// rv_pccd_fs::asset_reload, player build. Refreshing a resident asset is a
// development capability - only the dev channel ever asks for it - so this
// build carries the refusal and not the per-kind refresh that the real one
// in rv_pccd_fs_reload_devtools.cpp performs.
#include "rv_pconsole/cd/rv_pccd_fs.hpp"

#include "pdk/rv_err.h"

namespace rv_3dmppc {

int64_t rv_pccd_fs::asset_reload(const char* /*resname*/, rv_cd_resource_kind& /*kind_out*/) {
    return RV_ERR_NOENT;
}

int64_t rv_pccd_fs::asset_refresh(const char* /*resname*/, const void* /*bytes*/, int64_t /*nbytes*/,
                                   rv_cd_resource_kind& /*kind_out*/) {
    return RV_ERR_NOENT;
}

}  // namespace rv_3dmppc
