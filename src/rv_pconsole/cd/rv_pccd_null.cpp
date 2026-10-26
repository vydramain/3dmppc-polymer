#include "rv_pconsole/cd/rv_pccd_null.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

int rv_pccd_null::evaluate(const rv_pdklib::rv_manifest_budget & /*budget*/, int64_t &bytes, std::string & /*error*/)
{
    bytes = 0;
    return RV_OK;
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

int64_t rv_pccd_null::resource_size(rv_cd_resource_kind /*kind*/, const char * /*resname*/)
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

int64_t rv_pccd_null::asset_reload(const char * /*resname*/, rv_cd_resource_kind & /*kind_out*/)
{
    return RV_ERR_INVAL;
}

int64_t rv_pccd_null::stream_play(const char *resname, const rv_cd_stream_conf *conf)
{
    if (resname == nullptr || conf == nullptr) {
        RV_LOG_WARN("pccd", "stream play with a null resname or conf");
        return RV_ERR_INVAL;
    }
    // An empty drive has no streamed entry: no such entry, not a device failure.
    RV_LOG_WARN("pccd", "stream play refused: the drive is empty");
    return RV_ERR_NOENT;
}

int64_t rv_pccd_null::stream_stop()
{
    // No stream was ever started: nothing to stop.
    return RV_OK;
}

int64_t rv_pccd_null::stream_status(rv_cd_stream_info *out)
{
    if (out == nullptr) {
        RV_LOG_WARN("pccd", "stream status with a null out");
        return RV_ERR_INVAL;
    }
    out->state = RV_CD_STREAM_IDLE;
    out->error = 0;
    out->position = 0;
    out->loops = 0;
    out->starved_frames = 0;
    return RV_OK;
}

int64_t rv_pccd_null::stream_pump()
{
    // No stream, so no medium read and no portion to put into the ring.
    return RV_OK;
}

} // namespace rv_3dmppc
