#pragma once

// Integer twin of rv_scene_local_trs: out = T * Ry * Rx * Rz * S. r = R * S in 4.12, t = position in Q20.12.
// Angles snap to table steps (1/4096 turn), so r also differs from rv_scene_local_trs by the snap.

#include <cstdint>
#include <limits>

#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_trig.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace rv_pdklib
{

// Ry*Rx*Rz is an exact product of 4.12 factors, so it carries 3 * 12 fraction bits; one rounding drops them.
inline constexpr int gte_rot_product_bits = 3 * gte_frac_bits;
// Largest entry of R*S in 4.12: |R*S| < 8.0, symmetric (no -32768).
inline constexpr std::int64_t gte_trs_entry_max = std::numeric_limits<std::int16_t>::max();
// Products past 2^51 round to 2^15 or more, so they are refused before the multiply. |rot| <= 2^36 and an fx scale
// (|scale| <= 2^31) keep int64 safe, the negations included.
inline constexpr std::int64_t gte_trs_product_max = (gte_trs_entry_max + 1) << gte_rot_product_bits;

// Internal: 3x3 in row-major int64; a plain product keeps the scale 4096^k of its factors.
struct gte_rot3 {
    std::int64_t m[9];
};

// Internal: rotations as rv_scene_rotate_x/y/z build them, with 4.12 entries from the table (x, y, z).
inline gte_rot3 gte_rot_x(std::int32_t index)
{
    const std::int64_t s = rv_gte_sin(index);
    const std::int64_t c = rv_gte_cos(index);
    return { { gte_one, 0, 0, 0, c, -s, 0, s, c } };
}

inline gte_rot3 gte_rot_y(std::int32_t index)
{
    const std::int64_t s = rv_gte_sin(index);
    const std::int64_t c = rv_gte_cos(index);
    return { { c, 0, s, 0, gte_one, 0, -s, 0, c } };
}

inline gte_rot3 gte_rot_z(std::int32_t index)
{
    const std::int64_t s = rv_gte_sin(index);
    const std::int64_t c = rv_gte_cos(index);
    return { { c, -s, 0, s, c, 0, 0, 0, gte_one } };
}

// Internal: exact product a * b, no rounding.
inline gte_rot3 gte_rot_mul(const gte_rot3 &a, const gte_rot3 &b)
{
    gte_rot3 res{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) {
                res.m[i * 3 + j] += a.m[i * 3 + k] * b.m[k * 3 + j];
            }
        }
    }
    return res;
}

// Internal: out = rot * scale in 4.12, rounded once, half away from zero. RV_ERR_INVAL when it leaves int16;
// `out` is then untouched.
[[nodiscard]] inline int gte_trs_entry(std::int64_t rot, fx scale, std::int16_t &out)
{
    const std::int64_t wide = scale;
    const std::int64_t mag_rot = rot < 0 ? -rot : rot;
    const std::int64_t mag_scale = wide < 0 ? -wide : wide;
    if (mag_rot != 0 && mag_scale > gte_trs_product_max / mag_rot) {
        return RV_ERR_INVAL;
    }
    const std::int64_t product = rot * wide;
    const std::int64_t half = std::int64_t{ 1 } << (gte_rot_product_bits - 1);
    const std::int64_t biased = product < 0 ? product - half : product + half;
    const std::int64_t e = biased / (std::int64_t{ 1 } << gte_rot_product_bits);
    if (e > gte_trs_entry_max || e < -gte_trs_entry_max) {
        return RV_ERR_INVAL;
    }
    out = static_cast<std::int16_t>(e);
    return RV_OK;
}

// out = T * Ry * Rx * Rz * S; rotation is Q20.12 degrees as [x, y, z].
// Returns RV_ERR_INVAL when an entry of R*S leaves its 4.12 range; `out` is then untouched.
[[nodiscard]] inline int rv_gte_trs(fx_vec3 position, fx_vec3 rotation, fx_vec3 scale, rv_gte_matrix &out)
{
    const gte_rot3 ry = gte_rot_y(rv_gte_angle_index(rotation.y));
    const gte_rot3 rx = gte_rot_x(rv_gte_angle_index(rotation.x));
    const gte_rot3 rz = gte_rot_z(rv_gte_angle_index(rotation.z));
    const gte_rot3 rot = gte_rot_mul(gte_rot_mul(ry, rx), rz);
    // Column j of R*S is column j of R times scale j.
    const fx scales[3] = { scale.x, scale.y, scale.z };
    rv_gte_matrix res{};
    for (int k = 0; k < 9; ++k) {
        if (gte_trs_entry(rot.m[k], scales[k % 3], res.r[k]) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    res.t[0] = position.x;
    res.t[1] = position.y;
    res.t[2] = position.z;
    out = res;
    return RV_OK;
}

} // namespace rv_pdklib
