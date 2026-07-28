#pragma once

#include <cstdint>

#include "pdk/de/rv_de.hpp"

namespace rv_pdk {

inline constexpr int RV_3DMPPC_VERSION_MAJOR = 0;
inline constexpr int RV_3DMPPC_VERSION_MINOR = 0;

inline constexpr const char* RV_ABI_INFO_SECTION = ".mppc_abi";

struct rv_mppc_abi_info {
    char magic[8];  // MPPCABI\0 form marker
    uint32_t version_major;
    uint32_t version_minor;
};

static_assert(sizeof(rv_mppc_abi_info) == 16);

inline constexpr const char* RV_ABI_SYMBOL_CREATE = "mppc_disc_create";
inline constexpr const char* RV_ABI_SYMBOL_DESTROY = "mppc_disc_destroy";

using rv_disc_create_fn = rv_de* (*)(int version_major, int version_minor);
using rv_disc_destroy_fn = void (*)(rv_de* disc);

}  // namespace rv_pdk
