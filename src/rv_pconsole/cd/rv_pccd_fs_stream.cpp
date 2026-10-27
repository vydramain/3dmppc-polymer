// rv_pccd_fs streamed music: forwards the rv_cd_stream_* calls to stream_ with the medium
// and the ring. Without audio attached (ca_ null) nothing can sound: play, queue and switch refuse
// with RV_ERR_IO after their argument and entry checks; stop, status, queue_status and pump treat a null ca_ as idle.
#include <cstdint>

#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cd/rv_pccd_fs.hpp"

namespace rv_3dmppc
{

int64_t rv_pccd_fs::stream_play(const char *resname, const rv_cd_stream_conf *conf)
{
    return stream_.play(*medium_, ca_, resname, conf);
}

int64_t rv_pccd_fs::stream_queue(const char *resname, const rv_cd_stream_conf *conf)
{
    return stream_.queue(*medium_, ca_, resname, conf);
}

int64_t rv_pccd_fs::stream_switch(const char *resname, const rv_cd_stream_conf *conf)
{
    return stream_.switch_to(*medium_, ca_, resname, conf);
}

int64_t rv_pccd_fs::stream_stop()
{
    if (ca_ == nullptr) {
        return RV_OK;
    }
    return stream_.stop(*ca_);
}

int64_t rv_pccd_fs::stream_status(rv_cd_stream_info *out)
{
    if (ca_ != nullptr) {
        return stream_.status(*ca_, out);
    }
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

int64_t rv_pccd_fs::stream_queue_status(rv_cd_stream_queue_info *out)
{
    if (ca_ != nullptr) {
        return stream_.queue_status(*ca_, out);
    }
    if (out == nullptr) {
        RV_LOG_WARN("pccd", "stream queue status with a null out");
        return RV_ERR_INVAL;
    }
    out->pending = 0;
    out->switches = 0;
    return RV_OK;
}

int64_t rv_pccd_fs::stream_pump()
{
    if (ca_ == nullptr) {
        return RV_OK;
    }
    return stream_.pump(*medium_, *ca_);
}

} // namespace rv_3dmppc
