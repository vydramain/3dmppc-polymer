#include "rv_pconsole/cd/rv_pccd_null.hpp"

#include "pdk/rv_err.h"

namespace rv_3dmppc
{

rv_pcbudget_cost rv_pccd_null::evaluate(const rv_pdklib::rv_manifest_budget & /*budget*/)
{
    return {};
}

int64_t rv_pccd_null::asset_open(const char * /*resname*/)
{
    // An empty drive is a legal machine: no such entry, not a device failure.
    return RV_ERR_NOENT;
}

int64_t rv_pccd_null::asset_size(int64_t /*handle*/)
{
    // asset_open never issues a handle, so no handle is valid.
    return RV_ERR_INVAL;
}

int64_t rv_pccd_null::asset_read(int64_t /*handle*/, void * /*baddr*/, int64_t /*baddr_size*/)
{
    return RV_ERR_INVAL;
}

int64_t rv_pccd_null::resource_addr(rv_cd_resource_kind /*kind*/, const char * /*resname*/)
{
    return RV_ERR_INVAL;
}

int64_t rv_pccd_null::resource_palette_addr(rv_cd_resource_kind /*kind*/, const char * /*resname*/)
{
    return RV_ERR_INVAL;
}

int64_t rv_pccd_null::resource_width(rv_cd_resource_kind /*kind*/, const char * /*resname*/)
{
    return RV_ERR_INVAL;
}

int64_t rv_pccd_null::resource_height(rv_cd_resource_kind /*kind*/, const char * /*resname*/)
{
    return RV_ERR_INVAL;
}

int64_t rv_pccd_null::texture_reload(const char * /*resname*/)
{
    return RV_ERR_INVAL;
}

} // namespace rv_3dmppc
