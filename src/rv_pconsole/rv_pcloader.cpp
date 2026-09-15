// rv_pcloader: mounting the archive and the disc's load/unload lifecycle.
#include "rv_pconsole/rv_pcloader.hpp"

#include <dlfcn.h>
#include <elf.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <new>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"
#include "pdklib/rv_disc_hash/rv_disc_hash.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pconsole/cd/rv_pczip.hpp"
#include "rv_pconsole/rv_pcloader_detail.hpp"

namespace rv_3dmppc
{

namespace
{

// The two service entries. They live in the archive next to the assets but are
// NOT part of the disc's resource namespace: the console reads them, the game
// never asks for them.
constexpr const char *RV_PCLOADER_MANIFEST_ENTRY = "disc.toml";
constexpr const char *RV_PCLOADER_DEFAULT_CODE_ENTRY = "disc.so";

// Ceilings on what the console will allocate on the say-so of an archive. Every
// size below comes out of a file that may have been downloaded from anywhere,
// so none of them may become a malloc argument unchecked: a two-line header
// claiming a four-gigabyte manifest must cost a log line, not the machine's
// memory.
constexpr int64_t RV_PCLOADER_MANIFEST_MAX_SIZE = 1 << 20; // 1 MiB of text is already absurd
constexpr int64_t RV_PCLOADER_CODE_MAX_SIZE = 128 << 20;   // 128 MiB of code likewise

} // namespace

namespace rv_pcloader_detail
{

// Outcome of pulling one whole entry out of the archive, as a sentence fit for
// a log line. Empty means success.
std::string read_whole_entry(const rv_zipreader &zip, const char *name,
    int64_t max_size,
    std::vector<unsigned char> &out)
{
    const int64_t size = zip.size(name);
    if (size < 0) {
        return "no such entry in the archive";
    }
    if (size > max_size) {
        return std::format("entry is {} bytes, over the {} byte ceiling", size,
            max_size);
    }

    try {
        out.assign(static_cast<std::size_t>(size), 0);
    } catch (const std::bad_alloc &) {
        return "out of memory";
    }
    if (size == 0) {
        return std::string();
    }

    int64_t nread = 0;
    switch (zip.read(name, out.data(), size, nread)) {
    case rv_zipread::ok:
        break;
    case rv_zipread::not_found:
        return "no such entry in the archive";
    case rv_zipread::short_buffer:
        return "the entry grew between measuring and reading it";
    case rv_zipread::corrupt:
        return "the archive's bookkeeping for this entry does not hold up";
    case rv_zipread::crc_mismatch:
        return "checksum mismatch - the entry is not the bytes that were written";
    case rv_zipread::io_error:
        return "the host file failed the read";
    }
    if (nread != size) {
        return "short read";
    }
    return std::string();
}

} // namespace rv_pcloader_detail

rv_pcloader::~rv_pcloader()
{
    unload();
}

void rv_pcloader::notify_initialized(const rv_de *disc)
{
    if (disc_ != nullptr && disc == disc_) {
        initialized_ = true;
    }
}

// Which entry of the archive carries the code. The manifest names it; a
// manifest that leaves the key blank falls back to the conventional name.
// Both stages ask this, and they must agree: the version check reads the very
// entry the extraction later maps.
static std::string code_entry_of(const rv_pdklib::rv_manifest &manifest)
{
    return manifest.budget.pccd.code_entry.empty() ? RV_PCLOADER_DEFAULT_CODE_ENTRY : manifest.budget.pccd.code_entry;
}

int64_t rv_pcloader::mount(const char *archive_path)
{
    unload();

    // Check that a disc path was given at all
    if (archive_path == nullptr || *archive_path == '\0') {
        RV_LOG_ERR("pcloader", "no disc path was given");
        return RV_ERR_INVAL;
    }

    // Check the file.
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(archive_path),
            ec)) {
        RV_LOG_ERR("pcloader",
            "no disc at '{}': the path does not name a readable file",
            rv_pdklib::rv_log_escape(archive_path));
        return RV_ERR_NOENT;
    }

    // Check the container. zip_ is a member so the archive can stay open past
    // this function's return, for bring_up() to read from later.
    auto zip = std::make_unique<rv_zipreader>();
    std::string zip_error;
    if (!zip->open(archive_path, zip_error)) {
        RV_LOG_ERR("pcloader", "'{}' is not a readable .mppcdisc archive: {}",
            rv_pdklib::rv_log_escape(archive_path),
            rv_pdklib::rv_log_escape(zip_error.c_str(), 160));
        return RV_ERR_IO;
    }

    // Check the manifest.
    std::vector<unsigned char> manifest_bytes;
    std::string why =
        read_whole_entry(*zip, RV_PCLOADER_MANIFEST_ENTRY, RV_PCLOADER_MANIFEST_MAX_SIZE, manifest_bytes);
    if (!why.empty()) {
        RV_LOG_ERR("pcloader", "'{}' carries no usable '{}': {}",
            rv_pdklib::rv_log_escape(archive_path), RV_PCLOADER_MANIFEST_ENTRY, why);
        return RV_ERR_NOENT;
    }

    std::string manifest_text(
        reinterpret_cast<const char *>(manifest_bytes.data()),
        manifest_bytes.size());

    // The origin argument is what puts `disc.toml:14:` in front of every
    // diagnostic instead of a bare `line 14:` - the report is the only thing
    // the console can say about a manifest it could not read, so it says it in
    // the shape the burner's own messages have.
    // The console parses the whole manifest with the burner's own parser (one
    // shared schema, rv_pdklib::rv_manifest) but ACTS only on the disc's
    // identity/title, the code entry and [budget.*]; [scripts] sources is read
    // only for the all-or-none check below, and [build], [assets], [textures]
    // are never read back out here.
    std::string merror;
    const int64_t mres =
        rv_pdklib::rv_manifest_parse(manifest_text, RV_PCLOADER_MANIFEST_ENTRY, manifest_, merror);

    if (mres != 0) {
        // The disc has no name yet: the manifest that would have given it one
        // is exactly what failed, so `manifest_` is still empty and the archive
        // path is the only honest identity here.
        //
        // The report lists one mistake per LINE, and rv_log_escape turns those
        // newlines into \x0A rather than letting an archive forge log lines. It
        // stays one long line on purpose, and the length is raised well past the
        // 64-byte default so that a manifest with several mistakes is not cut
        // down to its first one.
        RV_LOG_ERR("pcloader", "'{}' carries a '{}' that does not parse: {}",
            rv_pdklib::rv_log_escape(archive_path),
            RV_PCLOADER_MANIFEST_ENTRY,
            rv_pdklib::rv_log_escape(merror.c_str(), 512));
        return RV_ERR_INVAL;
    }


    // The lua machine, cross-checked against what this archive can actually
    // serve. The code checksum below covers disc.so and NOTHING else: a
    // manifest edited after burning, or a .luac taken out of the archive,
    // moves no checksum at all. So the declaration is checked here, from the
    // bytes, and a disagreement is a broken disc rather than a disc with less
    // scripting in it - the console must not run something it cannot honour.
    //
    // Whatever that entry script pulls in afterwards is not checked and is not
    // meant to be: the console knows the one name the disc gave it.
    const rv_pdklib::rv_manifest_budget_pccl &pccl = manifest_.budget.pccl;
    const bool lua_scripts = !manifest_.scripts_sources.empty();
    const bool lua_memory = pccl.script_memory_size > 0;
    const bool lua_entry = !pccl.script_entry.empty();

    // A disc is a lua disc or a C++ disc. All three statements, or none: a
    // manifest carrying some of them describes a machine this console cannot
    // build, and the honest answer to that is a refusal, not a guess about
    // which of the three the author meant.
    if (lua_scripts != lua_memory || lua_memory != lua_entry) {
        RV_LOG_ERR("pcloader",
            "disc '{}' is neither a lua disc nor a C++ disc: [scripts] sources {}, "
            "script_memory_size={}, script_entry='{}'. All three or none",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            lua_scripts ? "stated" : "absent", pccl.script_memory_size,
            rv_pdklib::rv_log_escape(pccl.script_entry.c_str()));
        return RV_ERR_INVAL;
    }

    if (lua_entry && !zip->has(pccl.script_entry.c_str())) {
        RV_LOG_ERR("pcloader",
            "disc '{}' names '{}' as its lua entry, but the drive has no such asset",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            rv_pdklib::rv_log_escape(pccl.script_entry.c_str()));
        return RV_ERR_INVAL;
    }

    const std::string code_entry = code_entry_of(manifest_);

    // Version verdict from BYTES, BEFORE dlopen. A disc built against
    // a different PDK sees the console's structs at the wrong offsets, and that
    // failure does not announce itself: it is garbage geometry, a silent
    // corruption, a crash three minutes into play. dlopen would already run the
    // disc's constructors, so the verdict is read straight from the code
    // entry's ELF note (elf(5)) while it is still nothing but bytes in a buffer
    // - the mismatched code is never mapped at all. This is the ONLY version
    // check: the factory below creates the disc and decides nothing.
    if (pre_dlopen_check(zip.get(), code_entry.c_str()) < 0) {
        RV_LOG_ERR("pcloader",
            "disc '{}' failed the pre-load inspection of its code entry - "
            "the exact "
            "refusal is in the log line above; its code will not be mapped",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()));
        return RV_ERR_INVAL;
    }

    // Every check above passed straight from bytes; the archive is now handed
    // to bring_up() to read the code from, whenever it is called.
    zip_ = std::move(zip);
    return RV_OK;
}

int64_t rv_pcloader::bring_up()
{
    if (zip_ == nullptr) {
        RV_LOG_ERR("pcloader",
            "bring_up() called with no disc mounted; call mount() first");
        return RV_ERR_INVAL;
    }

    const std::string code_entry = code_entry_of(manifest_);
    // (4) The code entry, and the extraction the OS loader forces on us - see
    // the note at the top of rv_pcloader.hpp: dlopen maps a file, so the code
    // needs an inode of its own before it can be anything but bytes in a zip.
    std::vector<unsigned char> code;
    std::string why = read_whole_entry(*zip_, code_entry.c_str(), RV_PCLOADER_CODE_MAX_SIZE, code);
    if (!why.empty()) {
        RV_LOG_ERR("pcloader",
            "disc '{}' names its code entry '{}', which is unusable: {}",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            rv_pdklib::rv_log_escape(code_entry.c_str()), why);
        return RV_ERR_NOENT;
    }

    why = extract_code(code, temp_path_);
    if (!why.empty()) {
        RV_LOG_ERR("pcloader", "cannot stage the code of disc '{}' for loading: {}",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()), why);
        unload();
        return RV_ERR_IO;
    }

    // (5) RTLD_NOW resolves every relocation right here: a disc missing a symbol
    // must fail on the loading screen, where the message is readable, and not
    // forty minutes in when the code path that needed it finally runs. RTLD_LOCAL
    // keeps the disc's symbols out of the process-global namespace, so two discs
    // - or a disc and the console - cannot capture each other's names by
    // accident, and nothing the disc exports beyond the two ABI symbols is
    // reachable by anyone.
    handle_ = ::dlopen(temp_path_.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle_ == nullptr) {
        const char *dl_error = ::dlerror();
        RV_LOG_ERR(
            "pcloader", "dlopen of disc '{}' failed: {}",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            rv_pdklib::rv_log_escape(dl_error != nullptr ? dl_error : "no reason given", 160));
        unload();
        return RV_ERR_IO;
    }

    // (6) Both symbols or neither. A disc that can be created but not destroyed
    // is not half-loadable, it is a leak with a vtable - and the only code that
    // may destroy the object is the code that made it (pdk/de/rv_dv.h).
    ::dlerror(); // clear any stale error before the lookups
    auto create = reinterpret_cast<rv_mppc_disc_create_fn>(
        ::dlsym(handle_, RV_MPPC_DISC_ENTRY_CREATE));
    auto destroy = reinterpret_cast<rv_mppc_disc_destroy_fn>(
        ::dlsym(handle_, RV_MPPC_DISC_ENTRY_DESTROY));
    if (create == nullptr || destroy == nullptr) {
        RV_LOG_ERR("pcloader",
            "disc '{}' exports no {}(); it was not built with "
            "RV_MPPC_DISC_ENTRY_DEF",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            create == nullptr ? RV_MPPC_DISC_ENTRY_CREATE : RV_MPPC_DISC_ENTRY_DESTROY);
        unload();
        return RV_ERR_INVAL;
    }

    // Version compatibility was settled BEFORE this point: pre_dlopen_check reads
    // the note, ahead of dlopen. create() takes no part in that decision - it is a
    // pure factory, and a nullptr from it means the disc refused to be created,
    // not a verdict about the version.
    rv_de *disc = nullptr;
    try {
        disc = create();
    } catch (...) {
        // An exception escaping an extern "C" factory is not something the ABI
        // permits, so this is defence and not a contract: a badly built disc must
        // still not take the console down with it.
        disc = nullptr;
    }
    if (disc == nullptr) {
        RV_LOG_ERR("pcloader",
            "disc '{}' returned no object from {}(); refusing the disc",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()), RV_MPPC_DISC_ENTRY_CREATE);
        unload();
        return RV_ERR_INVAL;
    }

    disc_ = disc;
    destroy_ = destroy;

    RV_LOG_INFO(
        "pcloader", "loaded disc '{}' ('{}') from '{}' at 3dmppc version {}.{}",
        rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
        rv_pdklib::rv_log_escape(manifest_.disc_title.c_str()),
        rv_pdklib::rv_log_escape(zip_->path().c_str()), (int)RV_MPPC_VER_MAJOR, (int)RV_MPPC_VER_MINOR);
    return RV_OK;
}

int64_t rv_pcloader::load(const char *archive_path)
{
    const int64_t mount_rc = mount(archive_path);
    if (mount_rc < 0) {
        return mount_rc;
    }
    return bring_up();
}

// RAII - the teardown order lives here and nowhere else, and it is the
// exact reverse of construction:
//
//   disc_shutdown() -> mppc_disc_destroy() -> dlclose() -> unlink()
//
// dlclose AFTER destroy, never before: dlclose unmaps the library's text, and
// the destructor is IN that text. Destroying afterwards calls a function whose
// instructions are no longer mapped - the process jumps into a hole, at
// shutdown, where the crash looks like anything but its cause. The unlink comes
// last for the same shape of reason: while the code is mapped, the file is what
// the kernel pages from.
void rv_pcloader::unload()
{
    if (disc_ != nullptr) {
        // rv_de.h: the hook runs after the last frame and NOT for a disc that
        // refused to start. The facade is still valid at this point - that is
        // precisely why it runs before destroy and before dlclose.
        if (initialized_) {
            try {
                disc_->disc_shutdown(disc_->self);
            } catch (...) {
                RV_LOG_ERR("pcloader",
                    "disc_shutdown() of '{}' threw; tearing down anyway",
                    rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()));
            }
        }
        if (destroy_ != nullptr) {
            destroy_(disc_);
        }
    }
    disc_ = nullptr;
    destroy_ = nullptr;
    initialized_ = false;

    if (handle_ != nullptr) {
        ::dlclose(handle_);
        handle_ = nullptr;
    }

    if (!temp_path_.empty()) {
        // The staging file has done its job the moment the code is unmapped.
        // Removing it is not tidiness: it is an executable copy of somebody's
        // game sitting in a world-readable directory.
        if (::unlink(temp_path_.c_str()) != 0 && errno != ENOENT) {
            RV_LOG_WARN("pcloader", "could not remove the staging file '{}': {}",
                temp_path_, std::strerror(errno));
        }
        temp_path_.clear();
    }

    // Drop the archive last: reset() destroys the rv_zipreader, which closes
    // its file handle - the one piece of teardown mount() introduced that the
    // rest of this function did not know about before.
    zip_.reset();
}

} // namespace rv_3dmppc
