#include "rv_pccd.hpp"

#include "pdk/rv_err.hpp"

// Operations return RV_ERR_IO until the backend lands (stage 5).
int64_t rv_3dmppc::rv_pccd::asset_open(const char*) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pccd::asset_size(int64_t) { return RV_ERR_IO; }
int64_t rv_3dmppc::rv_pccd::asset_read(int64_t, void*, int64_t) { return RV_ERR_IO; }
