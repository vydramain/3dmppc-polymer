// rv_pconsole_cmd: the development command dispatcher, player build.
// 3DMPPC_DEVTOOLS is OFF, so cmd_ is never engaged - every entry point is a
// no-op.
#include "rv_pconsole/rv_pconsole.hpp"

void rv_3dmppc::rv_pconsole::cmd_service()
{
}

void rv_3dmppc::rv_pconsole::cmd_after_frame()
{
}

void rv_3dmppc::rv_pconsole::cmd_note_pause()
{
}

void rv_3dmppc::rv_pconsole::cmd_dispatch(const rv_pccmdreq & /*req*/)
{
}

void rv_3dmppc::rv_pconsole::cmd_status(int64_t /*id*/)
{
}

void rv_3dmppc::rv_pconsole::cmd_reload(const rv_pccmdreq & /*req*/)
{
}

void rv_3dmppc::rv_pconsole::cmd_get(const rv_pccmdreq & /*req*/)
{
}

void rv_3dmppc::rv_pconsole::cmd_asset(const rv_pccmdreq & /*req*/)
{
}
