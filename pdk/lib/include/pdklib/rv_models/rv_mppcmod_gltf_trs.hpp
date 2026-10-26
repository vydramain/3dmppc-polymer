// glTF node transforms in int64: each number goes to Q.16 once, and the matrix is built the way cgltf lays it out.
// Header-only and free of cgltf types; the glTF reader is the only caller.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "pdk/rv_err.h"
#include "pdklib/rv_models/rv_mppcmod_quant.hpp"

namespace rv_pdklib
{

namespace detail
{

// Bound in Q.16 on each glTF matrix entry and TRS factor: 2^14 (2^30 in Q.16). Below it a product of two is below
// 2^60, so a sum of four is below 2^62.
inline constexpr int64_t RV_MPPCMOD_GLTF_ENTRY_LIMIT = int64_t{ 1 } << 30;
// glTF matrices are 4x4 and column-major; the last column holds the translation.
inline constexpr size_t RV_MPPCMOD_GLTF_MATRIX_DIM = 4;
inline constexpr size_t RV_MPPCMOD_GLTF_MATRIX_FLOATS = RV_MPPCMOD_GLTF_MATRIX_DIM * RV_MPPCMOD_GLTF_MATRIX_DIM;
// Lanes of a translation, a scale and a 3x3 rotation side; a quaternion has four lanes (x, y, z, w).
inline constexpr size_t RV_MPPCMOD_GLTF_VEC_LANES = 3;
inline constexpr size_t RV_MPPCMOD_GLTF_QUAT_LANES = 4;
inline constexpr size_t RV_MPPCMOD_GLTF_ROT_ENTRIES = RV_MPPCMOD_GLTF_VEC_LANES * RV_MPPCMOD_GLTF_VEC_LANES;
// 1.0 in Q.32: the scale of a product of two Q.16 values.
inline constexpr int64_t RV_MPPCMOD_GLTF_ONE_Q32 = rv_mppcmod_q16_one * rv_mppcmod_q16_one;

using q16_matrix = std::array<int64_t, RV_MPPCMOD_GLTF_MATRIX_FLOATS>;

// True when |v| < limit. Compares without negating, so INT64_MIN is refused too.
inline bool within(__int128 v, int64_t limit)
{
    return v > -limit && v < limit;
}

// ponytail: __int128 as in is_mirrored (rv_mppcmod_gltf.cpp); replace when MSVC arrives.
// rv_mppcmod_round_shift for __int128: q / 2^bits, half away from zero.
inline __int128 round_shift128(__int128 q, int bits)
{
    const __int128 divisor = static_cast<__int128>(1) << bits;
    const __int128 half = divisor / 2;
    return (q < 0 ? q - half : q + half) / divisor;
}

// Lanes of glTF numbers to Q.16, each once. RV_ERR_INVAL if one is not finite or reaches `limit` in magnitude.
inline int lanes_to_q16(const float *src, size_t lanes, int64_t limit, int64_t *dst)
{
    for (size_t k = 0; k < lanes; ++k) {
        if (rv_mppcmod_scalar_to_q16(src[k], dst[k]) != RV_OK || !within(dst[k], limit)) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

inline bool entries_within(const q16_matrix &m, int64_t limit)
{
    return std::all_of(m.begin(), m.end(), [limit](int64_t v) {
        return within(v, limit);
    });
}

// Column-major Q.16 matrix of translation t, rotation quaternion r (x, y, z, w) and scale s: column c is the rotation's
// column c times s[c], the last column is t. Each entry is the rotation (Q.32) times the scale (Q.16), in Q.48, rounded
// to Q.16 once: within 0.5 Q.16 unit of the exact TRS of the Q.16 inputs. Against the file's exact TRS, for a unit
// quaternion (glTF requires one; cgltf does not check), an entry is within 2.9*|s| + 1 Q.16 units: 2*sqrt(2) times |s|
// for a diagonal entry, 2 times |s| for an off-diagonal one, 0.5 for rounding the scale input, 0.5 for the final rounding.
// RV_ERR_INVAL if a number is not finite or reaches the entry bound.
inline int trs_to_q16(const float *t, const float *r, const float *s, q16_matrix &out)
{
    std::array<int64_t, RV_MPPCMOD_GLTF_VEC_LANES> trans = {};
    std::array<int64_t, RV_MPPCMOD_GLTF_QUAT_LANES> quat = {};
    std::array<int64_t, RV_MPPCMOD_GLTF_VEC_LANES> scale = {};
    if (lanes_to_q16(t, RV_MPPCMOD_GLTF_VEC_LANES, RV_MPPCMOD_GLTF_ENTRY_LIMIT, trans.data()) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (lanes_to_q16(r, RV_MPPCMOD_GLTF_QUAT_LANES, RV_MPPCMOD_GLTF_ENTRY_LIMIT, quat.data()) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (lanes_to_q16(s, RV_MPPCMOD_GLTF_VEC_LANES, RV_MPPCMOD_GLTF_ENTRY_LIMIT, scale.data()) != RV_OK) {
        return RV_ERR_INVAL;
    }

    const int64_t x = quat[0];
    const int64_t y = quat[1];
    const int64_t z = quat[2];
    const int64_t w = quat[3];
    // 2 * (a * b + c * d) in Q.32. The inputs are Q.16 below 2^30, so the result is below 2^62.
    const auto twice = [](int64_t a, int64_t b, int64_t c, int64_t d) {
        return 2 * (a * b + c * d);
    };
    constexpr int64_t one = RV_MPPCMOD_GLTF_ONE_Q32;
    // Standard quaternion rotation matrix, column-major.
    const std::array<int64_t, RV_MPPCMOD_GLTF_ROT_ENTRIES> rot32 = {
        one - twice(y, y, z, z),
        twice(x, y, z, w),
        twice(x, z, -y, w),
        twice(x, y, -z, w),
        one - twice(x, x, z, z),
        twice(y, z, x, w),
        twice(x, z, y, w),
        twice(y, z, -x, w),
        one - twice(x, x, y, y),
    };
    for (size_t c = 0; c < RV_MPPCMOD_GLTF_VEC_LANES; ++c) {
        for (size_t row = 0; row < RV_MPPCMOD_GLTF_VEC_LANES; ++row) {
            // Q.32 x Q.16 is Q.48; 2 * q16_bits shifts it back to Q.16.
            const __int128 product = static_cast<__int128>(rot32[RV_MPPCMOD_GLTF_VEC_LANES * c + row]) * scale[c];
            const __int128 entry = round_shift128(product, 2 * rv_mppcmod_q16_bits);
            if (!within(entry, RV_MPPCMOD_GLTF_ENTRY_LIMIT)) {
                return RV_ERR_INVAL;
            }
            out[RV_MPPCMOD_GLTF_MATRIX_DIM * c + row] = static_cast<int64_t>(entry);
        }
        out[RV_MPPCMOD_GLTF_MATRIX_DIM * c + RV_MPPCMOD_GLTF_VEC_LANES] = 0;
    }
    for (size_t k = 0; k < RV_MPPCMOD_GLTF_VEC_LANES; ++k) {
        out[RV_MPPCMOD_GLTF_MATRIX_DIM * RV_MPPCMOD_GLTF_VEC_LANES + k] = trans[k];
    }
    out[RV_MPPCMOD_GLTF_MATRIX_FLOATS - 1] = rv_mppcmod_q16_one;
    if (!entries_within(out, RV_MPPCMOD_GLTF_ENTRY_LIMIT)) {
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

} // namespace detail

} // namespace rv_pdklib
