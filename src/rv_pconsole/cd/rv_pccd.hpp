// The rv_cd contract made abstract. Which drive answers a name is the
// console's construction-time choice (rv_pccd_fs or rv_pccd_null).
#pragma once

#include <cstdint>
#include <memory>

#include "pdk/cd/rv_cd.h"
#include "rv_pconsole/cd/rv_pcmedium.hpp"

namespace rv_3dmppc
{

class rv_pccv;
class rv_pcca;

// texture_reload result meaning nothing holds that name resident, so there
// was nothing to refresh.
constexpr int64_t RV_PCCD_NOT_RESIDENT = 1;

class rv_pccd
{
public:
    virtual ~rv_pccd() = default;

    rv_pccd(const rv_pccd &) = delete;
    rv_pccd &operator=(const rv_pccd &) = delete;

    virtual int64_t asset_open(const char *resname) = 0;

    virtual int64_t asset_size(int64_t handle) = 0;

    virtual int64_t asset_read(int64_t handle, void *baddr, int64_t baddr_size) = 0;

    // A disc never acquires or releases a resource - it only ever names one.
    // The drive makes a name resident on the first of these five calls to ask
    // for it, and keeps it resident until the disc that named it is
    // unloaded, at which point the drive frees everything it made resident
    // (see rv_pccd_fs's destructor) - the disc never frees anything itself.
    // `kind` is rv_cd.h's rv_cd_resource_kind; TEXTURE and AUDIO exist today,
    // and any other value - or a query that does not describe the kind it
    // was asked of (see rv_cd.h's own comment for the table) - is refused
    // (see rv_pccd_fs's resource_resolve_, the one place that refusal lives).
    virtual int64_t resource_addr(rv_cd_resource_kind kind, const char *resname) = 0;

    // AUDIO-only: its resident byte length. See rv_cd.h.
    virtual int64_t resource_size(rv_cd_resource_kind kind, const char *resname) = 0;

    virtual int64_t resource_palette_addr(rv_cd_resource_kind kind, const char *resname) = 0;

    virtual int64_t resource_width(rv_cd_resource_kind kind, const char *resname) = 0;

    virtual int64_t resource_height(rv_cd_resource_kind kind, const char *resname) = 0;

    // Console-side only - neither is reached through the extern "C" block.
    virtual void medium_insert(std::unique_ptr<rv_pcmedium> medium) = 0;

    // Refreshes a resident texture in place (same residency id, new addresses
    // and size). Returns RV_OK when refreshed, RV_PCCD_NOT_RESIDENT when
    // nothing holds that name resident (nothing to refresh - the next name
    // query reads the current bytes), a negative rv_err when the refresh
    // failed and the old texture stays.
    // Console-side only, reached by the dev channel, never by a game.
    virtual int64_t texture_reload(const char *resname) = 0;

    // Where a resident texture uploads to. Borrowed - cv_ outlives cd_ for the
    // whole run - because rv_pconsole builds cd_ before cv_ exists (cl_'s
    // shutdown-order requirement pins that declaration order), so cd cannot
    // take cv by constructor reference the way rv_pccl_luajit takes cd.
    virtual void video_attach(rv_pccv &cv) = 0;

    // audio_attach's twin: where a resident AUDIO resource uploads to.
    // Borrowed the same way and for the same reason - ca_ outlives cd_ for
    // the whole run, and rv_pconsole builds cd_ after ca_ already exists but
    // still cannot take it by constructor reference (see video_attach above).
    virtual void audio_attach(rv_pcca &ca) = 0;

    virtual bool valid() const = 0;

protected:
    rv_pccd() = default;
};

} // namespace rv_3dmppc
