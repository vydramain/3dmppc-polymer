#include "rv_pconsole/cv/rv_pccv_null.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

rv_pccv_null::rv_pccv_null(const rv_pccv_conf &conf)
    : conf_(conf)
{
    RV_LOG_INFO("pccv", "video off ({}x{} declared, no rasterizer, no-op)", conf_.screen_width,
        conf_.screen_height);
}

// --- hardware geometry -------------------------------------------------------

int64_t rv_pccv_null::screen_width()
{
    return conf_.screen_width;
}
int64_t rv_pccv_null::screen_height()
{
    return conf_.screen_height;
}
int64_t rv_pccv_null::texture_max_width()
{
    return conf_.texture_max_width;
}
int64_t rv_pccv_null::texture_max_height()
{
    return conf_.texture_max_height;
}
int64_t rv_pccv_null::video_memory_size()
{
    return conf_.video_memory_size;
}
int64_t rv_pccv_null::frame_capacity()
{
    return conf_.frame_capacity;
}

// --- video RAM ---------------------------------------------------------------

int64_t rv_pccv_null::video_asset_malloc(int64_t /*size*/)
{
    // Nothing is allocated, repeats are fine: a fixed, positive, fake
    // address every disc gets back. There is no pool behind it to run out.
    constexpr int64_t RV_PCCV_NULL_FAKE_ADDR = 16;
    return RV_PCCV_NULL_FAKE_ADDR;
}

int64_t rv_pccv_null::video_asset_write(int64_t /*addr*/, const rv_texture * /*texture*/)
{
    return RV_OK;
}

int64_t rv_pccv_null::video_asset_free(int64_t /*addr*/)
{
    return RV_OK;
}

// --- the frame ---------------------------------------------------------------

int64_t rv_pccv_null::frame_configure(uint64_t /*config*/, rv_color /*clear_color*/)
{
    return RV_OK;
}

int64_t rv_pccv_null::frame_put(const rv_primitive * /*primitive*/)
{
    return RV_OK;
}

int64_t rv_pccv_null::frame_flush()
{
    return RV_OK;
}

// --- console-side --------------------------------------------------------------

int64_t rv_pccv_null::screen_open(const char * /*title*/, uint64_t /*scale*/)
{
    // Opens nothing: there is no window in this machine.
    return RV_OK;
}

void rv_pccv_null::dump_last_frame(const std::string & /*path*/) const
{
    // Unreachable with a non-empty path: the boot refuses cv=null together
    // with --dump-frame before this console exists.
}

} // namespace rv_3dmppc
