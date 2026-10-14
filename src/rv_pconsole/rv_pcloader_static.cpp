// rv_pcloader's static half: the disc's code is linked into the console, so nothing is
// staged, dlopen-ed or ELF-checked. The version is checked against the disc's note symbol.
#include "rv_pconsole/rv_pcloader.hpp"

#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"
#include "pdklib/rv_disc_version/rv_disc_version.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_version/rv_version.hpp"

// Defined by the linked disc: RV_MPPC_DISC_ENTRY_DEF and RV_MPPC_DISC_VERSION_DEF.
extern "C" rv_de *RV_MPPC_DISC_ENTRY_CREATE_DEF(void);
extern "C" void RV_MPPC_DISC_ENTRY_DESTROY_DEF(rv_de *disc);
extern "C" const rv_pdklib::rv_mppc_note rv_mppc_disc_version_note;

namespace rv_3dmppc
{

int rv_pcloader::open_code_(rv_mppc_disc_create_fn &create, rv_mppc_disc_destroy_fn &destroy)
{
    create = &RV_MPPC_DISC_ENTRY_CREATE_DEF;
    destroy = &RV_MPPC_DISC_ENTRY_DESTROY_DEF;
    return RV_OK;
}

// Nothing to undo: the code is part of this executable, so it is never unmapped
// before exit and there is no staging file to remove.
void rv_pcloader::close_code_()
{
}

int64_t rv_pcloader::pre_dlopen_check(rv_zipreader *, const char *)
{
    const rv_mppc_note_desc &desc = rv_mppc_disc_version_note.desc;
    if (!rv_pdklib::rv_version_compatible(desc.version_major, desc.version_minor)) {
        RV_LOG_ERR("pcloader",
            "disc version is incompatible with the current console version: "
            "disc version is: {}.{}; ",
            desc.version_major,
            desc.version_minor);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// No staging directory is used, so there is nothing to probe.
int64_t rv_pcloader_probe_staging()
{
    return RV_OK;
}

} // namespace rv_3dmppc
