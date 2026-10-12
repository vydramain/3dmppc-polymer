// rv_pcloader's dl half: the disc's code bytes come from the archive or the
// directory, are written to a staging file, and dlopen loads that file by path.
// dlclose and the unlink of the staging file undo it.
#include "rv_pconsole/rv_pcloader.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcloader_detail.hpp"

namespace rv_3dmppc
{

namespace
{

// The directory route already read and pre_dlopen_check-ed these exact bytes
// in mount_dir(). Re-reading disc.so here would reopen the very race this
// whole path exists to close: the bytes just verified must be the bytes that
// get mapped, and an unpacked directory - unlike an archive - can be
// rewritten by a live burner run at any moment while this session is mounted.
// Moving the buffer out is a straight reuse, not a second read.
int take_dir_code(std::vector<unsigned char> &from, std::vector<unsigned char> &to)
{
    to = std::move(from);
    return RV_OK;
}

} // namespace

// Extract the code entry, dlopen it and look up both ABI symbols. Returns RV_OK
// with create and destroy set, or the rv_err after logging the reason. Staging,
// dlopen and dlsym failures call unload() first; an unreadable code entry does
// not, so the mounted archive stays mounted.
int rv_pcloader::open_code_(rv_mppc_disc_create_fn &create, rv_mppc_disc_destroy_fn &destroy)
{
    const std::string code_entry = code_entry_of(manifest_);
    // (4) The code entry, and the extraction the OS loader forces on us - see
    // the note at the top of rv_pcloader.hpp: dlopen maps a file, so the code
    // needs an inode of its own before it can be anything but bytes in a zip.
    std::vector<unsigned char> code;
    std::string why;
    const int rc = from_directory_ ? take_dir_code(dir_code_, code) :
                                     read_whole_entry(*zip_, code_entry.c_str(), RV_PCLOADER_CODE_MAX_SIZE, code, why);
    if (rc != RV_OK) {
        RV_LOG_ERR("pcloader",
            "disc '{}' names its code entry '{}', which is unusable: {}",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            rv_pdklib::rv_log_escape(code_entry.c_str()),
            why);
        return RV_ERR_NOENT;
    }

    const int staged = extract_code(code, temp_path_, why);
    if (staged != RV_OK) {
        RV_LOG_ERR("pcloader",
            "cannot stage the code of disc '{}' for loading: {}",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            why);
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
        RV_LOG_ERR("pcloader",
            "dlopen of disc '{}' failed: {}",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            rv_pdklib::rv_log_escape(dl_error != nullptr ? dl_error : "no reason given", RV_PCLOADER_SYSTEM_ERROR_MAX_LEN));
        unload();
        return RV_ERR_IO;
    }

    // (6) Both symbols or neither. A disc that can be created but not destroyed
    // is not half-loadable, it is a leak with a vtable - and the only code that
    // may destroy the object is the code that made it (pdk/de/rv_dv.h).
    ::dlerror(); // clear any stale error before the lookups
    create = reinterpret_cast<rv_mppc_disc_create_fn>(::dlsym(handle_, RV_MPPC_DISC_ENTRY_CREATE));
    destroy = reinterpret_cast<rv_mppc_disc_destroy_fn>(::dlsym(handle_, RV_MPPC_DISC_ENTRY_DESTROY));
    if (create == nullptr || destroy == nullptr) {
        RV_LOG_ERR("pcloader",
            "disc '{}' exports no {}(); it was not built with "
            "RV_MPPC_DISC_ENTRY_DEF",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            create == nullptr ? RV_MPPC_DISC_ENTRY_CREATE : RV_MPPC_DISC_ENTRY_DESTROY);
        unload();
        return RV_ERR_INVAL;
    }

    return RV_OK;
}

// The dl half of unload(): dlclose first, then the staging file goes. Called
// after destroy, never before it - see the teardown note in rv_pcloader.cpp.
void rv_pcloader::close_code_()
{
    if (handle_ != nullptr) {
        ::dlclose(handle_);
        handle_ = nullptr;
    }

    if (!temp_path_.empty()) {
        // The staging file has done its job the moment the code is unmapped.
        // Removing it is not tidiness: it is an executable copy of somebody's
        // game sitting in a world-readable directory.
        if (::unlink(temp_path_.c_str()) != 0 && errno != ENOENT) {
            RV_LOG_WARN("pcloader", "could not remove the staging file '{}': {}", temp_path_, std::strerror(errno));
        }
        temp_path_.clear();
    }
}

} // namespace rv_3dmppc
