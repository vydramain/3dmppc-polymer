// rv_pcloader: the disc's load/unload lifecycle - dlopen, the ABI symbols, and
// the one safe teardown order.
#include "rv_pconsole/rv_pcloader.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcloader_detail.hpp"

namespace rv_3dmppc
{

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

int64_t rv_pcloader::bring_up()
{
    if (zip_ == nullptr && !from_directory_) {
        RV_LOG_ERR("pcloader",
            "bring_up() called with no disc mounted; call mount() first");
        return RV_ERR_INVAL;
    }

    const std::string code_entry = code_entry_of(manifest_);
    // (4) The code entry, and the extraction the OS loader forces on us - see
    // the note at the top of rv_pcloader.hpp: dlopen maps a file, so the code
    // needs an inode of its own before it can be anything but bytes in a zip.
    std::vector<unsigned char> code;
    std::string why;
    if (from_directory_) {
        // The directory route already read and pre_dlopen_check-ed these exact
        // bytes in mount_dir(). Re-reading disc.so here would reopen the very
        // race this whole path exists to close: the bytes just verified must
        // be the bytes that get mapped, and an unpacked directory - unlike an
        // archive - can be rewritten by a live burner run at any moment while
        // this session is mounted. Moving the buffer out is a straight reuse,
        // not a second read.
        code = std::move(dir_code_);
    } else {
        why = read_whole_entry(*zip_, code_entry.c_str(), RV_PCLOADER_CODE_MAX_SIZE, code);
        if (!why.empty()) {
            RV_LOG_ERR("pcloader",
                "disc '{}' names its code entry '{}', which is unusable: {}",
                rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
                rv_pdklib::rv_log_escape(code_entry.c_str()), why);
            return RV_ERR_NOENT;
        }
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
            "RV_MPPC_DISC_DEF",
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
        rv_pdklib::rv_log_escape(from_directory_ ? dir_path_.c_str() : zip_->path().c_str()),
        (int)RV_MPPC_VER_MAJOR, (int)RV_MPPC_VER_MINOR);
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
// A disc that throws out of its last hook is still a disc being torn down: the
// catch must not stop destroy_ or dlclose from running. Its own function
// because try/catch inside two ifs inside unload() is one level of nesting past
// what this tree allows, and the nesting is the part that reads badly.
void rv_pcloader::shutdown_disc_()
{
    try {
        disc_->disc_shutdown(disc_->self);
    } catch (...) {
        RV_LOG_ERR("pcloader", "disc_shutdown() of '{}' threw; tearing down anyway",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()));
    }
}

void rv_pcloader::unload()
{
    if (disc_ != nullptr) {
        // rv_de.h: the hook runs after the last frame and NOT for a disc that
        // refused to start. The facade is still valid at this point - that is
        // precisely why it runs before destroy and before dlclose.
        if (initialized_) {
            shutdown_disc_();
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

    // The directory route's state, reset the same way for the same reason:
    // an idempotent teardown must leave nothing behind that the next mount()
    // or mount_dir() could mistake for this one's.
    from_directory_ = false;
    dir_path_.clear();
    dir_code_.clear();
    code_hash_.clear();
}

} // namespace rv_3dmppc
