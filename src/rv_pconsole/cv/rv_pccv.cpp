#include "rv_pccv.hpp"

#include "pdk/rv_err.hpp"

// Operations return RV_ERR_IO until the backend lands (stage 3).
int64_t rv_3dmppc::rv_pccv::screen_width() { return conf_.screen_width; }
int64_t rv_3dmppc::rv_pccv::screen_height() { return conf_.screen_height; }
int64_t rv_3dmppc::rv_pccv::texture_max_width() { return conf_.texture_max_width; }
int64_t rv_3dmppc::rv_pccv::texture_max_height() { return conf_.texture_max_height; }
int64_t rv_3dmppc::rv_pccv::video_memory_size() { return conf_.video_memory_size; }
int64_t rv_3dmppc::rv_pccv::frame_capacity() { return conf_.frame_capacity; }
int64_t rv_3dmppc::rv_pccv::video_asset_malloc(int64_t) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pccv::video_asset_write(int64_t, rv_texture) {
    return RV_ERR_IO;
}
int64_t rv_3dmppc::rv_pccv::video_asset_free(int64_t) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pccv::frame_configure(uint64_t, rv_color) {
    return RV_ERR_IO;
}
int64_t rv_3dmppc::rv_pccv::frame_put(rv_primitive) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pccv::frame_flush() { return RV_ERR_IO; }
