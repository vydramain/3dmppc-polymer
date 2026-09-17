// rv_pcloader: the archive mount - pulling whole entries out of a
// .mppcdisc zip and turning it into a checked disc.
#include "rv_pconsole/rv_pcloader.hpp"

#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pconsole/cd/rv_zipreader.hpp"
#include "rv_pconsole/rv_pcloader_detail.hpp"

namespace rv_3dmppc
{

namespace
{

// The manifest's service entry. It lives in the archive next to the assets but
// is NOT part of the disc's resource namespace: the console reads it, the game
// never asks for it. (The code entry's name is the other one, and lives in
// rv_pcloader_detail.hpp because bring_up() needs it too.)
constexpr const char *RV_PCLOADER_MANIFEST_ENTRY = "disc.toml";

// Ceilings on what the console will allocate on the say-so of an archive. Every
// size below comes out of a file that may have been downloaded from anywhere,
// so none of them may become a malloc argument unchecked: a two-line header
// claiming a four-gigabyte manifest must cost a log line, not the machine's
// memory. (The code ceiling states the same policy in
// rv_pcloader_detail.hpp, where both mount routes and bring_up() can see it.)
constexpr int64_t RV_PCLOADER_MANIFEST_MAX_SIZE = 1 << 20; // 1 MiB of text is already absurd

} // namespace

namespace
{

// Check that a disc path was given at all, and that it names a readable file.
// The first stage of mount(): everything after this needs an actual file to
// open.
int64_t check_archive_path(const char *archive_path)
{
    if (archive_path == nullptr || *archive_path == '\0') {
        RV_LOG_ERR("pcloader", "no disc path was given");
        return RV_ERR_INVAL;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(archive_path),
            ec)) {
        RV_LOG_ERR("pcloader",
            "no disc at '{}': the path does not name a readable file",
            rv_pdklib::rv_log_escape(archive_path));
        return RV_ERR_NOENT;
    }
    return RV_OK;
}

// Opens the container. zip_ is a member so the archive can stay open past
// mount()'s return, for bring_up() to read from later; `out_zip` is handed
// back for mount() to keep only once every later check has passed.
int64_t open_archive(const char *archive_path,
    std::unique_ptr<rv_zipreader> &out_zip)
{
    out_zip = std::make_unique<rv_zipreader>();
    std::string zip_error;
    if (!out_zip->open(archive_path, zip_error)) {
        RV_LOG_ERR("pcloader", "'{}' is not a readable .mppcdisc archive: {}",
            rv_pdklib::rv_log_escape(archive_path),
            rv_pdklib::rv_log_escape(zip_error.c_str(), 160));
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Reads the manifest entry whole and hands it back as text.
int64_t read_manifest_text(const rv_zipreader &zip, const char *archive_path,
    std::string &out_text)
{
    std::vector<unsigned char> manifest_bytes;
    std::string why = read_whole_entry(zip, RV_PCLOADER_MANIFEST_ENTRY,
        RV_PCLOADER_MANIFEST_MAX_SIZE, manifest_bytes);
    if (!why.empty()) {
        RV_LOG_ERR("pcloader", "'{}' carries no usable '{}': {}",
            rv_pdklib::rv_log_escape(archive_path), RV_PCLOADER_MANIFEST_ENTRY, why);
        return RV_ERR_NOENT;
    }

    out_text.assign(reinterpret_cast<const char *>(manifest_bytes.data()),
        manifest_bytes.size());
    return RV_OK;
}

// The origin argument is what puts `disc.toml:14:` in front of every
// diagnostic instead of a bare `line 14:` - the report is the only thing
// the console can say about a manifest it could not read, so it says it in
// the shape the burner's own messages have.
// The console parses the whole manifest with the burner's own parser (one
// shared schema, rv_pdklib::rv_manifest) but ACTS only on the disc's
// identity/title, the code entry and [budget.*]; [scripts] sources is read
// only for the all-or-none check below, and [build], [assets], [textures]
// are never read back out here.
int64_t parse_manifest(const std::string &manifest_text,
    const char *archive_path, rv_pdklib::rv_manifest &out_manifest)
{
    std::string merror;
    const int64_t mres =
        rv_pdklib::rv_manifest_parse(manifest_text, RV_PCLOADER_MANIFEST_ENTRY, out_manifest, merror);

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
    return RV_OK;
}

// A disc is a lua disc or a C++ disc. All three statements, or none: a
// manifest carrying some of them describes a machine this console cannot
// build, and the honest answer to that is a refusal, not a guess about
// which of the three the author meant.
int64_t check_lua_triple(const rv_pdklib::rv_manifest &manifest)
{
    const rv_pdklib::rv_manifest_budget_pccl &pccl = manifest.budget.pccl;
    const bool lua_scripts = !manifest.scripts_sources.empty();
    const bool lua_memory = pccl.script_memory_size > 0;
    const bool lua_entry = !pccl.script_entry.empty();

    if (lua_scripts != lua_memory || lua_memory != lua_entry) {
        RV_LOG_ERR("pcloader",
            "disc '{}' is neither a lua disc nor a C++ disc: [scripts] sources {}, "
            "script_memory_size={}, script_entry='{}'. All three or none",
            rv_pdklib::rv_log_escape(manifest.disc_id.c_str()),
            lua_scripts ? "stated" : "absent", pccl.script_memory_size,
            rv_pdklib::rv_log_escape(pccl.script_entry.c_str()));
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// The lua entry the manifest names must actually be an asset the archive
// carries - a blank script_entry (a C++ disc) has nothing to check here.
int64_t check_lua_entry_asset(const rv_pdklib::rv_manifest &manifest,
    const rv_zipreader &zip)
{
    const rv_pdklib::rv_manifest_budget_pccl &pccl = manifest.budget.pccl;
    if (!pccl.script_entry.empty() && !zip.has(pccl.script_entry.c_str())) {
        RV_LOG_ERR("pcloader",
            "disc '{}' names '{}' as its lua entry, but the drive has no such asset",
            rv_pdklib::rv_log_escape(manifest.disc_id.c_str()),
            rv_pdklib::rv_log_escape(pccl.script_entry.c_str()));
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

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
int64_t rv_pcloader::mount(const char *archive_path)
{
    unload();

    int64_t err = check_archive_path(archive_path);
    if (err < 0) {
        return err;
    }

    std::unique_ptr<rv_zipreader> zip;
    err = open_archive(archive_path, zip);
    if (err < 0) {
        return err;
    }

    std::string manifest_text;
    err = read_manifest_text(*zip, archive_path, manifest_text);
    if (err < 0) {
        return err;
    }

    err = parse_manifest(manifest_text, archive_path, manifest_);
    if (err < 0) {
        return err;
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
    err = check_lua_triple(manifest_);
    if (err < 0) {
        return err;
    }

    err = check_lua_entry_asset(manifest_, *zip);
    if (err < 0) {
        return err;
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

} // namespace rv_3dmppc
