#pragma once

#include <cstdint>

#include "pdk/de/rv_de.hpp"

namespace rv_pdk {

#define RV_MPPC_VERSION_INFO_SECTION_NAME ".mppc_pdk_ver";

inline constexpr int RV_MPPC_VERSION_MAJOR = 0;
inline constexpr int RV_MPPC_VERSION_MINOR = 0;

// отвечает за то, а какое значение смотрим
inline constexpr const char* RV_MPPC_VERSION_INFO_SECTION = RV_MPPC_VERSION_INFO_SECTION_NAME;

// отвечает за обозначение секции внутри библиотеки для чтения, без исполнения кода
struct rv_mppc_version_info {
    char magic[8];  // marker {'R', 'V', '_', 'M', 'P', 'P', 'C', '\0'}
    uint32_t version_major;
    uint32_t version_minor;
};

// гарантирует побайтовую иднетичность для любой реализации
static_assert(sizeof(rv_mppc_version_info) == 16);

inline constexpr const char* RV_MPPC_VERSION_SYMBOL_CREATE = "rv_mppc_disc_create";
inline constexpr const char* RV_MPPC_VERSION_SYMBOL_DESTROY = "rv_mppc_disc_destroy";

using rv_mppc_disc_create_fn = rv_de* (*)(int32_t version_major, int32_t version_minor);
using rv_mppc_disc_destroy_fn = void (*)(rv_de* disc);

}  // namespace rv_pdk
