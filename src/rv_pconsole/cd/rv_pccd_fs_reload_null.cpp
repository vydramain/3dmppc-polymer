// rv_pccd_fs::texture_reload, player build. Refreshing a resident texture is a
// development capability - only the dev channel ever asks for it - so this
// build carries the refusal and not the decode/upload/swap that the real one
// in rv_pccd_fs_reload.cpp performs.
#include "rv_pconsole/cd/rv_pccd_fs.hpp"

#include "pdk/rv_err.h"

namespace rv_3dmppc {

int64_t rv_pccd_fs::texture_reload(const char* /*resname*/) {
    return RV_ERR_NOENT;
}

}  // namespace rv_3dmppc
