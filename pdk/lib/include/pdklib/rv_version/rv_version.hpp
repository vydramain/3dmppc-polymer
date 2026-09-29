#pragma once

#include <cstdint>

#include "pdk/de/rv_dv.h"

// Every file format and channel version in the repository is defined against
// this PDK version, derived once from RV_MPPC_VER_MAJOR/MINOR so no format
// restates its own pair. A disc, memory card, texture or scene is compatible
// when its major equals RV_MPPC_VER_MAJOR and its minor is at most
// RV_MPPC_VER_MINOR; the dev channel, shared frame, build map and editor
// layout/view still require an exact match.

namespace rv_pdklib
{

/// "M.m", e.g. "0.3", built at compile time from the PDK macros.
inline constexpr char rv_version_str[] =
    RV_MPPC_STR_DEF(RV_MPPC_VER_MAJOR) "." RV_MPPC_STR_DEF(RV_MPPC_VER_MINOR);

static_assert(RV_MPPC_VER_MAJOR >= 0 && RV_MPPC_VER_MAJOR <= 0xFF, "major does not fit a packed byte");
static_assert(RV_MPPC_VER_MINOR >= 0 && RV_MPPC_VER_MINOR <= 0xFF, "minor does not fit a packed byte");

/// 16-bit packed PDK version: major << 8 | minor.
inline constexpr uint16_t rv_version_packed16 =
    static_cast<uint16_t>((RV_MPPC_VER_MAJOR << 8) | RV_MPPC_VER_MINOR);

/// 32-bit packed PDK version: major << 16 | minor.
inline constexpr uint32_t rv_version_packed32 =
    (static_cast<uint32_t>(RV_MPPC_VER_MAJOR) << 16) | static_cast<uint32_t>(RV_MPPC_VER_MINOR);

/// True when (major, minor) is compatible with this PDK version: same major,
/// minor at most RV_MPPC_VER_MINOR.
inline constexpr bool rv_version_compatible(uint32_t major, uint32_t minor)
{
    return major == static_cast<uint32_t>(RV_MPPC_VER_MAJOR) &&
           minor <= static_cast<uint32_t>(RV_MPPC_VER_MINOR);
}

} // namespace rv_pdklib
