// rv_pcloader: the disc's load/unload lifecycle and the one safe teardown order.
// The dlopen half (code extraction, dlopen/dlsym, dlclose, staging unlink) lives
// in rv_pcloader_dl.cpp; a static disc (3DMPPC_STATIC_DISC) uses rv_pcloader_static.cpp.
#include "rv_pconsole/rv_pcloader.hpp"

#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"
#include "pdklib/rv_logs/rv_logs.hpp"

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
        RV_LOG_ERR("pcloader", "bring_up() called with no disc mounted; call mount() first");
        return RV_ERR_INVAL;
    }

    rv_mppc_disc_create_fn create = nullptr;
    rv_mppc_disc_destroy_fn destroy = nullptr;
    const int opened = open_code_(create, destroy);
    if (opened != RV_OK) {
        return opened;
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
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
            RV_MPPC_DISC_ENTRY_CREATE);
        unload();
        return RV_ERR_INVAL;
    }

    disc_ = disc;
    destroy_ = destroy;

    RV_LOG_INFO("pcloader",
        "loaded disc '{}' ('{}') from '{}' at 3dmppc version {}.{}",
        rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()),
        rv_pdklib::rv_log_escape(manifest_.disc_title.c_str()),
        rv_pdklib::rv_log_escape(from_directory_ ? dir_path_.c_str() : zip_->path().c_str()),
        (int)RV_MPPC_VER_MAJOR,
        (int)RV_MPPC_VER_MINOR);
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

// RAII - the teardown order lives here and nowhere else. In the dl build (the
// default) it is the exact reverse of construction:
//
//   disc_shutdown() -> mppc_disc_destroy() -> dlclose() -> unlink()
//
// (A static disc, 3DMPPC_STATIC_DISC, has no dlclose or unlink: see rv_pcloader_static.cpp.)
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
        RV_LOG_ERR("pcloader",
            "disc_shutdown() of '{}' threw; tearing down anyway",
            rv_pdklib::rv_log_escape(manifest_.disc_id.c_str()));
    }
}

void rv_pcloader::unload()
{
    // rv_de.h: the hook runs after the last frame and NOT for a disc that
    // refused to start. The facade is still valid at this point - that is
    // precisely why it runs before destroy and before dlclose.
    const bool loaded = disc_ != nullptr;
    if (loaded && initialized_) {
        shutdown_disc_();
    }
    if (loaded && destroy_ != nullptr) {
        destroy_(disc_);
    }
    disc_ = nullptr;
    destroy_ = nullptr;
    initialized_ = false;

    close_code_();

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
