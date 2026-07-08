// 32-bit ARGB color used by the framebuffer.
#pragma once

#include <algorithm>
#include <cstdint>

#include "math/math.hpp"

namespace rv_3dmppc {

using Color = std::uint32_t;  // 0xAARRGGBB

inline Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return (0xFFu << 24) | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
}

inline Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return (std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
}

// Pack a linear [0,1] Vec3 into a color, clamping to the valid range.
inline Color fromVec3(const Vec3& c) {
    auto q = [](float v) -> std::uint8_t {
        return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return rgb(q(c.x), q(c.y), q(c.z));
}

}  // namespace rv_3dmppc
