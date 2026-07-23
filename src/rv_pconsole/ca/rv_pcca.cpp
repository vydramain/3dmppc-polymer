#include "rv_pcca.hpp"

#include "pdk/rv_err.hpp"

// Operations return RV_ERR_IO until the backend lands (stage 7).
int64_t rv_3dmppc::rv_pcca::voice_count() { return conf_.voice_count; }
int64_t rv_3dmppc::rv_pcca::sound_memory_size() { return conf_.sound_memory_size; }
int64_t rv_3dmppc::rv_pcca::sound_asset_malloc(int64_t) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pcca::sound_asset_write(int64_t, const rv_sample*) {
    return RV_ERR_IO;
}
int64_t rv_3dmppc::rv_pcca::sound_asset_free(int64_t) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pcca::voice_setup(const rv_voice_conf*) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pcca::voice_play(int64_t) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pcca::voice_stop(int64_t) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pcca::voice_status(int64_t) { return RV_ERR_IO; }
