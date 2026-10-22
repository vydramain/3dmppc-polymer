#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace rv_pdklib
{

// Integer counterparts of rv_xform's stages. View space: z > 0 is in front, as rv_xform's w.

// Cull modes: the same values as rv_cull_mode in rv_xform.hpp, which is not included here.
inline constexpr std::uint32_t gte_cull_none = 0;
inline constexpr std::uint32_t gte_cull_screen_cw = 1;
inline constexpr std::uint32_t gte_cull_screen_ccw = 2;

// Largest count * (far - near) for the depth key. Keeps (depth_min - depth_max) * u below 2^62,
// so the key arithmetic stays in int64 for any int32 depth range.
inline constexpr std::int64_t gte_key_span_max = std::int64_t{ 1 } << 30;

// Most points one depth key takes: a polygon has at most four vertices.
inline constexpr int gte_key_max_count = 4;

// Screen point in pixels, saturated to +-gte_screen_limit. Cull reads this, not the int16 copy.
// Why 2^30: a triangle inside that square has |area2| <= 2^62, so the int64 cross product cannot
// overflow. A point beyond 2^30 px is far outside any screen.
inline constexpr std::int32_t gte_screen_limit = std::int32_t{ 1 } << 30;

struct rv_gte_screen {
    std::int32_t x;
    std::int32_t y;
};

// The console's int16 vertex, saturated late: rv_xform culls before it saturates.
struct rv_gte_screen16 {
    std::int16_t x;
    std::int16_t y;
};

// Focal length h and screen centre (cx, cy), in pixels.
struct rv_gte_viewport {
    std::int32_t h;
    std::int32_t cx;
    std::int32_t cy;
};

// Near maps to depth_max and far to depth_min: a larger key is nearer, as in rv_xform.
struct rv_gte_depth_range {
    fx near_plane;
    fx far_plane;
    std::int32_t depth_min;
    std::int32_t depth_max;
};

// Internal: round(num / den) half up, den > 0; num may be negative.
inline std::int64_t rv_gte_div_round(std::int64_t num, std::int64_t den)
{
    std::int64_t q = num / den;
    std::int64_t r = num % den;
    if (r < 0) {
        q -= 1;
        r += den;
    }
    return 2 * r >= den ? q + 1 : q;
}

// Internal: clamp into int16. Saturate, not wrap: a wrapped point would jump to the far edge.
inline std::int16_t rv_gte_saturate16(std::int64_t v)
{
    if (v < std::numeric_limits<std::int16_t>::min()) {
        return std::numeric_limits<std::int16_t>::min();
    }
    if (v > std::numeric_limits<std::int16_t>::max()) {
        return std::numeric_limits<std::int16_t>::max();
    }
    return static_cast<std::int16_t>(v);
}

// The console's vertex: the int32 screen point saturated to int16. Call after the cull.
inline rv_gte_screen16 rv_gte_to_int16(rv_gte_screen p)
{
    return rv_gte_screen16{ rv_gte_saturate16(p.x), rv_gte_saturate16(p.y) };
}

// Internal: clamp a screen coordinate to +-gte_screen_limit.
inline std::int32_t rv_gte_clamp_screen(std::int64_t v)
{
    if (v < -gte_screen_limit) {
        return -gte_screen_limit;
    }
    if (v > gte_screen_limit) {
        return gte_screen_limit;
    }
    return static_cast<std::int32_t>(v);
}

// Internal: a + round((b - a) * t), t a Q20.12 fraction in [0, 4096]. Rounds half away from zero,
// as rv_gte_round_frac in rv_gte_matrix: the points are Q20.12 like the matrix's. The screen rounds
// half up instead, as rv_xform does.
inline std::int64_t rv_gte_lerp(fx a, fx b, std::int64_t t)
{
    return static_cast<std::int64_t>(a) + rv_gte_round_frac((static_cast<std::int64_t>(b) - a) * t);
}

// Fraction of the edge a_z -> b_z at which z crosses near_plane, Q20.12 (0..4096), truncated. Lerp
// the other components of the edge with it to get the cut point's colour and uv.
// Precondition: a_z < near_plane <= b_z.
inline std::int64_t rv_gte_near_t(fx a_z, fx b_z, fx near_plane)
{
    const std::int64_t num = (static_cast<std::int64_t>(near_plane) - a_z) << gte_frac_bits;
    return num / (static_cast<std::int64_t>(b_z) - a_z);
}

// Point where the edge a -> b crosses z = near_plane; z is set to near_plane exactly.
// Precondition: a.z < near_plane <= b.z. t is truncated to Q20.12, so each component can be off the exact
// crossing: x by at most |b.x - a.x| / 4096 + 0.5 raw, y by at most |b.y - a.y| / 4096 + 0.5 raw.
// Error: the precondition fails or x or y leaves fx; out is untouched.
[[nodiscard]] inline int rv_gte_near_clip(fx_vec3 a, fx_vec3 b, fx near_plane, fx_vec3 &out)
{
    const bool crosses = a.z < near_plane && near_plane <= b.z;
    if (!crosses) {
        return RV_ERR_INVAL;
    }
    const std::int64_t t = rv_gte_near_t(a.z, b.z, near_plane);
    const int rc = fx_vec3_store(rv_gte_lerp(a.x, b.x, t), rv_gte_lerp(a.y, b.y, t), near_plane, out);
    if (rc != RV_OK) {
        return rc;
    }
    return RV_OK;
}

// Screen point of a view-space point: x = cx + x * h / z, y = cy - y * h / z.
// Rounds half up, as rv_xform_saturate's floor(v + 0.5): an exact tie goes up (-2.5 -> -2), so the
// result is not mirror-symmetric, as in rv_xform. Saturates to +-gte_screen_limit.
// Error: z <= 0 (behind the eye or on its plane); out is untouched.
[[nodiscard]] inline int rv_gte_project(fx_vec3 p, rv_gte_viewport vp, rv_gte_screen &out)
{
    if (p.z <= 0) {
        return RV_ERR_INVAL;
    }
    const std::int64_t dx = rv_gte_div_round(static_cast<std::int64_t>(p.x) * vp.h, p.z);
    const std::int64_t dy = rv_gte_div_round(static_cast<std::int64_t>(p.y) * vp.h, p.z);
    out = rv_gte_screen{ rv_gte_clamp_screen(vp.cx + dx), rv_gte_clamp_screen(vp.cy - dy) };
    return RV_OK;
}

// Back-face cull on the screen-space area2 = cross(b - a, c - a), as rv_xform_culled. Exact for points
// within +-gte_screen_limit, which is what rv_gte_project returns. Beyond that the clamp can change the
// sign, so inputs must stay inside that square (products would overflow).
// Area 0 is culled in every mode that culls. True means the triangle is dropped.
inline bool rv_gte_culled(rv_gte_screen a, rv_gte_screen b, rv_gte_screen c, std::uint32_t cull)
{
    if (cull == gte_cull_none) {
        return false;
    }
    const std::int64_t ux = static_cast<std::int64_t>(b.x) - a.x;
    const std::int64_t uy = static_cast<std::int64_t>(b.y) - a.y;
    const std::int64_t vx = static_cast<std::int64_t>(c.x) - a.x;
    const std::int64_t vy = static_cast<std::int64_t>(c.y) - a.y;
    const std::int64_t area2 = ux * vy - uy * vx;
    if (area2 == 0) {
        return true;
    }
    if (cull == gte_cull_screen_cw) {
        return area2 > 0;
    }
    return area2 < 0;
}

// Ordering-table key from the mean view z of count points (1..4); z points to at least count values.
// The mean is clamped to [near, far] and mapped linearly onto [depth_max, depth_min], rounded half up
// as rv_xform_depth_key.
// Error: count out of range, far <= near, or the span exceeds gte_key_span_max; key is untouched.
[[nodiscard]] inline int rv_gte_depth_key(const fx *z, int count, rv_gte_depth_range range, std::int32_t &key)
{
    if (count < 1 || count > gte_key_max_count || range.far_plane <= range.near_plane) {
        return RV_ERR_INVAL;
    }
    // Everything is scaled by count so the mean z stays an integer: sum - near * count, over span * count.
    const std::int64_t n = count;
    const std::int64_t total = (static_cast<std::int64_t>(range.far_plane) - range.near_plane) * n;
    if (total > gte_key_span_max) {
        return RV_ERR_INVAL;
    }
    std::int64_t sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += z[i];
    }
    const std::int64_t above = std::clamp(sum - static_cast<std::int64_t>(range.near_plane) * n, std::int64_t{ 0 }, total);
    const std::int64_t step = static_cast<std::int64_t>(range.depth_min) - range.depth_max;
    key = static_cast<std::int32_t>(range.depth_max + rv_gte_div_round(step * above, total));
    return RV_OK;
}

} // namespace rv_pdklib
