// rv_pconsole_dev: the development command dispatcher, player build.
// 3DMPPC_DEVTOOLS is OFF, so dev_ is never engaged - every entry point is a
// no-op, the same shape as rv_pcloader_livedir_null.cpp.
#include "rv_pconsole/rv_pconsole.hpp"

void rv_3dmppc::rv_pconsole::dev_service()
{
}

void rv_3dmppc::rv_pconsole::dev_after_frame()
{
}

void rv_3dmppc::rv_pconsole::dev_note_pause()
{
}

void rv_3dmppc::rv_pconsole::dev_dispatch(const rv_pcdevreq & /*req*/)
{
}

void rv_3dmppc::rv_pconsole::dev_status(int64_t /*id*/)
{
}

void rv_3dmppc::rv_pconsole::dev_reload(const rv_pcdevreq & /*req*/)
{
}

void rv_3dmppc::rv_pconsole::dev_get(const rv_pcdevreq & /*req*/)
{
}

void rv_3dmppc::rv_pconsole::dev_asset(const rv_pcdevreq & /*req*/)
{
}
