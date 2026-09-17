// rv_pcloader: the live-directory mount, player build. 3DMPPC_DEVTOOLS is OFF,
// so the real mount is not compiled into this binary at all - this refuses
// instead, the same shape as rv_pccl_null / rv_pccd_null / rv_pcplatform_null.
#include "rv_pconsole/rv_pcloader.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

bool rv_devtools_built()
{
    return false;
}

int64_t rv_pcloader::mount_dir(const char * /*dir_path*/)
{
    RV_LOG_ERR("pcloader",
        "this console was built without the development runtime: booting from "
        "a loose directory needs -D3DMPPC_DEVTOOLS=ON");
    return RV_ERR_NOENT;
}

} // namespace rv_3dmppc
