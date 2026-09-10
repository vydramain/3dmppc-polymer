#include "rv_pconsole/cv/rv_pccv.hpp"

#include "pdk/cv/rv_cv.h"

// --- C contract (pdk/cv/rv_cv.h) ---------------------------------------------
// An rv_cv* handle and the address of an rv_pccv are the same address: exactly
// one implementation of the base lives behind the handle at a time (either
// rv_pccv_sdl3 or rv_pccv_null), reached here through a virtual call.

extern "C" int64_t rv_cv_screen_width(rv_cv *cv)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->screen_width();
}

extern "C" int64_t rv_cv_screen_height(rv_cv *cv)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->screen_height();
}

extern "C" int64_t rv_cv_texture_max_width(rv_cv *cv)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->texture_max_width();
}

extern "C" int64_t rv_cv_texture_max_height(rv_cv *cv)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->texture_max_height();
}

extern "C" int64_t rv_cv_video_memory_size(rv_cv *cv)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->video_memory_size();
}

extern "C" int64_t rv_cv_video_asset_malloc(rv_cv *cv, int64_t size)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->video_asset_malloc(size);
}

extern "C" int64_t rv_cv_video_asset_write(rv_cv *cv, int64_t addr, const rv_texture *texture)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->video_asset_write(addr, texture);
}

extern "C" int64_t rv_cv_video_asset_free(rv_cv *cv, int64_t addr)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->video_asset_free(addr);
}

extern "C" int64_t rv_cv_frame_capacity(rv_cv *cv)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->frame_capacity();
}

extern "C" int64_t rv_cv_frame_configure(rv_cv *cv, uint64_t config, rv_color clear_color)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->frame_configure(config, clear_color);
}

extern "C" int64_t rv_cv_frame_put(rv_cv *cv, const rv_primitive *primitive)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->frame_put(primitive);
}

extern "C" int64_t rv_cv_frame_flush(rv_cv *cv)
{
    return reinterpret_cast<rv_3dmppc::rv_pccv *>(cv)->frame_flush();
}
