#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "pdklib/rv_models/rv_mppcmod_int.hpp"

namespace rv_pdklib
{

// --- float model -> integer model: the one float step is detail::rv_mppcmod_scalar_to_q16, run by rv_mppcmod_to_q16 ---
//
// Each float goes to Q.16 once (rv_mppcmod_to_q16). Positions then take the largest vertex shift at which they
// fit int16; uvs go to 4.12 from the same Q.16 value.

/// Fraction bits of the Q.16 intermediate. 2^-16 m is far below the 2^-12 m step of the finest
/// shift, so the rounding that matters is the one to the shift.
inline constexpr int rv_mppcmod_q16_bits = 16;
/// 1.0 in Q.16.
inline constexpr int64_t rv_mppcmod_q16_one = int64_t{ 1 } << rv_mppcmod_q16_bits;
/// Magnitude limit of a float before Q.16: 2^40 m. Its Q.16 takes 56 bits of int64, which leaves
/// headroom for the rounding add and the shifts after it.
inline constexpr double rv_mppcmod_q16_limit_metres = 0x1p40;
/// The same limit in Q.16, 2^56: the bound on every Q.16 value the integer step accepts. Below it, round_shift's
/// add and std::abs cannot overflow int64.
inline constexpr int64_t rv_mppcmod_q16_limit = static_cast<int64_t>(rv_mppcmod_q16_limit_metres) * rv_mppcmod_q16_one;

/// The reader's output: every number of the file in Q.16, once. Colours, parts and triangles are copied.
struct rv_mppcmod_q16_vertex {
    std::array<int64_t, 3> pos = {}; ///< metres, Q.16
    std::array<int64_t, 2> uv = {};  ///< Q.16
    std::array<uint8_t, 3> color = {};
};

struct rv_mppcmod_q16_model {
    std::vector<rv_mppcmod_part> parts;
    std::vector<rv_mppcmod_q16_vertex> vertices;
    std::vector<rv_mppcmod_triangle> triangles;
};

namespace detail
{
// Q.16 value to `bits` fraction bits: divides by 2^bits, rounding half away from zero. Integer
// division truncates toward zero, so the half is added with the sign first (as rv_gte_round_frac).
inline constexpr int64_t rv_mppcmod_round_shift(int64_t q, int bits)
{
    const int64_t divisor = int64_t{ 1 } << bits;
    const int64_t half = divisor / 2;
    return (q < 0 ? q - half : q + half) / divisor;
}

inline constexpr bool rv_mppcmod_fits_i16(int64_t v)
{
    return v >= std::numeric_limits<int16_t>::min() && v <= std::numeric_limits<int16_t>::max();
}

// A Q.16 coordinate at vertex shift `shift`, before the int16 check.
inline constexpr int64_t rv_mppcmod_pos_at(int64_t q, int shift)
{
    return rv_mppcmod_round_shift(q, rv_mppcmod_q16_bits - shift);
}

// Float -> integer step: v to Q.16, half away from zero. A float times 2^16 is exact in
// double, so std::round sees the exact value. NaN and inf fail isfinite; large values fail the limit.
inline int rv_mppcmod_scalar_to_q16(float v, int64_t &out)
{
    const double metres = static_cast<double>(v);
    const bool in_range = std::isfinite(metres) && std::fabs(metres) < rv_mppcmod_q16_limit_metres;
    if (!in_range) {
        return RV_ERR_INVAL;
    }
    out = static_cast<int64_t>(std::round(metres * static_cast<double>(rv_mppcmod_q16_one)));
    return RV_OK;
}

// Converts one vertex's floats to Q.16. Returns RV_ERR_INVAL if any of them is not usable.
inline int rv_mppcmod_vertex_to_q16(const rv_mppcmod_vertex &src, rv_mppcmod_q16_vertex &dst)
{
    for (size_t k = 0; k < src.pos.size(); ++k) {
        if (rv_mppcmod_scalar_to_q16(src.pos[k], dst.pos[k]) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    for (size_t k = 0; k < src.uv.size(); ++k) {
        if (rv_mppcmod_scalar_to_q16(src.uv[k], dst.uv[k]) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Refuses a Q.16 position or uv at or beyond +-2^56. Compares without negating, so INT64_MIN is refused too.
inline int rv_mppcmod_q16_in_range(const rv_mppcmod_q16_model &q, std::string &error)
{
    for (size_t v = 0; v < q.vertices.size(); ++v) {
        for (int64_t c : q.vertices[v].pos) {
            if (c <= -rv_mppcmod_q16_limit || c >= rv_mppcmod_q16_limit) {
                error = "vertex " + std::to_string(v) + " has a position beyond the 2^40 m limit of the Q.16 model";
                return RV_ERR_INVAL;
            }
        }
        for (int64_t c : q.vertices[v].uv) {
            if (c <= -rv_mppcmod_q16_limit || c >= rv_mppcmod_q16_limit) {
                error = "vertex " + std::to_string(v) + " has a uv beyond the 2^56 Q.16 limit";
                return RV_ERR_INVAL;
            }
        }
    }
    return RV_OK;
}

inline bool rv_mppcmod_fits_at(const rv_mppcmod_q16_model &q, int shift)
{
    for (const rv_mppcmod_q16_vertex &v : q.vertices) {
        for (int64_t c : v.pos) {
            if (!rv_mppcmod_fits_i16(rv_mppcmod_pos_at(c, shift))) {
                return false;
            }
        }
    }
    return true;
}

// Largest shift at which every position fits int16. Scanning down from the maximum, the first
// shift that fits is the largest one. An empty model has no coordinate to limit it, so the first
// shift tried, the maximum, fits and it gets rv_mppcmod_int_max_shift.
inline int rv_mppcmod_pick_shift(const rv_mppcmod_q16_model &q, int &shift, std::string &error)
{
    for (int s = rv_mppcmod_int_max_shift; s >= 0; --s) {
        if (rv_mppcmod_fits_at(q, s)) {
            shift = s;
            return RV_OK;
        }
    }
    int64_t largest = 0;
    for (const rv_mppcmod_q16_vertex &v : q.vertices) {
        for (int64_t c : v.pos) {
            largest = std::max(largest, std::abs(c));
        }
    }
    error = "has a coordinate of magnitude at least " + std::to_string(largest / rv_mppcmod_q16_one) +
        " m, which rounds outside the int16 range at shift 0";
    return RV_ERR_INVAL;
}

// Integer vertices at `shift`, colours, parts and triangles copied. A uv outside [-8, 8) is named in `error`.
inline int rv_mppcmod_build(const rv_mppcmod_q16_model &q, int shift, rv_mppcmod_int_model &model, std::string &error)
{
    model.shift = static_cast<uint8_t>(shift);
    model.parts = q.parts;
    model.triangles = q.triangles;
    model.vertices.assign(q.vertices.size(), {});
    for (size_t v = 0; v < q.vertices.size(); ++v) {
        const rv_mppcmod_q16_vertex &src = q.vertices[v];
        rv_mppcmod_int_vertex &dst = model.vertices[v];
        dst.color = src.color;
        for (size_t k = 0; k < dst.pos.size(); ++k) {
            dst.pos[k] = static_cast<int16_t>(rv_mppcmod_pos_at(src.pos[k], shift));
        }
        for (size_t k = 0; k < dst.uv.size(); ++k) {
            const int64_t uv = rv_mppcmod_round_shift(src.uv[k], rv_mppcmod_q16_bits - rv_mppcmod_uv_bits);
            if (!rv_mppcmod_fits_i16(uv)) {
                error = "vertex " + std::to_string(v) + " has a uv outside [-8, 8), the 4.12 range";
                return RV_ERR_INVAL;
            }
            dst.uv[k] = static_cast<int16_t>(uv);
        }
    }
    return RV_OK;
}
} // namespace detail

/// Float model to Q.16 model: every float to Q.16 once. A failing vertex is named in `error`; `out` is then untouched.
inline int rv_mppcmod_to_q16(const rv_mppcmod &in, rv_mppcmod_q16_model &out, std::string &error)
{
    rv_mppcmod_q16_model q;
    q.parts = in.parts;
    q.triangles = in.triangles;
    q.vertices.assign(in.vertices.size(), {});
    for (size_t v = 0; v < in.vertices.size(); ++v) {
        if (detail::rv_mppcmod_vertex_to_q16(in.vertices[v], q.vertices[v]) != RV_OK) {
            error = "vertex " + std::to_string(v) +
                " has a position or uv that is not finite or is beyond the 2^40 limit of the Q.16 step";
            return RV_ERR_INVAL;
        }
        q.vertices[v].color = in.vertices[v].color;
    }
    out = std::move(q);
    return RV_OK;
}

/// Quantises a Q.16 model to the integer layout: the largest vertex shift at which the positions fit int16,
/// uvs to 4.12. A position must satisfy |v| < 2^40 m; a uv must satisfy |v| < 2^56 Q.16 (2^40 in its unit).
/// A larger one is refused.
/// RV_OK means the result passes rv_mppcmod_int_validate, so rv_mppcmod_int_write accepts it.
/// Otherwise returns RV_ERR_INVAL with `error` naming the vertex, the coordinate or the broken rule,
/// and leaves `out` untouched.
inline int rv_mppcmod_quantize(const rv_mppcmod_q16_model &in, rv_mppcmod_int_model &out, std::string &error)
{
    const int in_range = detail::rv_mppcmod_q16_in_range(in, error);
    if (in_range != RV_OK) {
        return in_range;
    }
    int shift = 0;
    const int picked = detail::rv_mppcmod_pick_shift(in, shift, error);
    if (picked != RV_OK) {
        return picked;
    }
    rv_mppcmod_int_model model;
    const int built = detail::rv_mppcmod_build(in, shift, model, error);
    if (built != RV_OK) {
        return built;
    }
    const int checked = rv_mppcmod_int_validate(model, error);
    if (checked != RV_OK) {
        return checked;
    }
    out = std::move(model);
    return RV_OK;
}

/// Quantises a float model: rv_mppcmod_to_q16, then the Q.16 overload. The same refusals and the same `out` rule apply.
inline int rv_mppcmod_quantize(const rv_mppcmod &in, rv_mppcmod_int_model &out, std::string &error)
{
    rv_mppcmod_q16_model q;
    const int converted = rv_mppcmod_to_q16(in, q, error);
    if (converted != RV_OK) {
        return converted;
    }
    return rv_mppcmod_quantize(q, out, error);
}

} // namespace rv_pdklib
