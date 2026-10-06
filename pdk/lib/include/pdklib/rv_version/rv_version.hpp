#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"

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

// Splits "M.m" into major/minor: RV_OK on success, RV_ERR_INVAL on empty parts,
// non-digits, no dot, or a part over 3 digits (major/minor fit a byte;
// the static_asserts above hold that, so a longer run cannot be a valid part).
inline int rv_version_parse(std::string_view text, uint32_t &major, uint32_t &minor)
{
    const size_t dot = text.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == text.size()) {
        return RV_ERR_INVAL;
    }
    const std::string_view a = text.substr(0, dot);
    const std::string_view b = text.substr(dot + 1);
    if (text.find('.', dot + 1) != std::string_view::npos) {
        return RV_ERR_INVAL;
    }
    for (const std::string_view part : { a, b }) {
        if (part.empty() || part.size() > 3 ||
            !std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            return RV_ERR_INVAL;
        }
    }
    major = static_cast<uint32_t>(std::stoul(std::string(a)));
    minor = static_cast<uint32_t>(std::stoul(std::string(b)));
    return RV_OK;
}

} // namespace rv_pdklib
