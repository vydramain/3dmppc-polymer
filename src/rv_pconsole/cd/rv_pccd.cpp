#include "rv_pconsole/cd/rv_pccd.hpp"

#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

// --- C contract (pdk/cd/rv_cd.h) ---------------------------------------------
// An rv_cd* handle and the address of an rv_pccd are the same address: which
// concrete class actually lives there is a console construction-time choice
// (rv_pccd_fs or rv_pccd_null), reached here through a virtual call.

extern "C" int64_t rv_cd_asset_open(rv_cd *cd, const char *resname)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->asset_open(resname);
}

extern "C" int64_t rv_cd_asset_size(rv_cd *cd, int64_t handle)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->asset_size(handle);
}

extern "C" int64_t rv_cd_asset_read(rv_cd *cd, int64_t handle, void *baddr, int64_t baddr_size)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->asset_read(handle, baddr, baddr_size);
}

extern "C" int64_t rv_cd_resource_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->resource_addr(kind, resname);
}

extern "C" int64_t rv_cd_resource_size(rv_cd *cd, rv_cd_resource_kind kind, const char *resname)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->resource_size(kind, resname);
}

extern "C" int64_t rv_cd_resource_palette_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->resource_palette_addr(kind, resname);
}

extern "C" int64_t rv_cd_resource_width(rv_cd *cd, rv_cd_resource_kind kind, const char *resname)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->resource_width(kind, resname);
}

extern "C" int64_t rv_cd_resource_height(rv_cd *cd, rv_cd_resource_kind kind, const char *resname)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->resource_height(kind, resname);
}

extern "C" int64_t rv_cd_stream_play(rv_cd *cd, const char *resname, const rv_cd_stream_conf *conf)
{
    if (cd == nullptr) {
        RV_LOG_WARN("pccd", "stream play with a null cd");
        return RV_ERR_INVAL;
    }
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->stream_play(resname, conf);
}

extern "C" int64_t rv_cd_stream_stop(rv_cd *cd)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->stream_stop();
}

extern "C" int64_t rv_cd_stream_status(rv_cd *cd, rv_cd_stream_info *out)
{
    if (cd == nullptr) {
        RV_LOG_WARN("pccd", "stream status with a null cd");
        return RV_ERR_INVAL;
    }
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->stream_status(out);
}

extern "C" int64_t rv_cd_stream_queue(rv_cd *cd, const char *resname, const rv_cd_stream_conf *conf)
{
    if (cd == nullptr) {
        RV_LOG_WARN("pccd", "stream queue with a null cd");
        return RV_ERR_INVAL;
    }
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->stream_queue(resname, conf);
}

extern "C" int64_t rv_cd_stream_switch(rv_cd *cd, const char *resname, const rv_cd_stream_conf *conf)
{
    if (cd == nullptr) {
        RV_LOG_WARN("pccd", "stream switch with a null cd");
        return RV_ERR_INVAL;
    }
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->stream_switch(resname, conf);
}

extern "C" int64_t rv_cd_stream_queue_status(rv_cd *cd, rv_cd_stream_queue_info *out)
{
    if (cd == nullptr) {
        RV_LOG_WARN("pccd", "stream queue status with a null cd");
        return RV_ERR_INVAL;
    }
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->stream_queue_status(out);
}
