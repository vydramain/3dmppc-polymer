#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "pdk/rv_err.h"

namespace rv_pdklib
{

// Fixed-point transform: r is row-major 4.12 (rotation times scale), t is Q20.12 translation.
struct rv_gte_matrix {
    std::int16_t r[9];
    std::int32_t t[3];
};

static_assert(sizeof(rv_gte_matrix) == 32, "rv_gte_matrix: 18 bytes of r, 2 of padding, 12 of t");
static_assert(offsetof(rv_gte_matrix, t) == 20, "rv_gte_matrix: t follows r at offset 20");

// Fractional bits of Q20.12 and of 4.12; 1.0 in 4.12 is 4096.
inline constexpr int gte_frac_bits = 12;
inline constexpr std::int32_t gte_one = 1 << gte_frac_bits;

// Identity transform: r is 1.0 on the diagonal, t is zero.
inline constexpr rv_gte_matrix gte_identity{
    { gte_one, 0, 0, 0, gte_one, 0, 0, 0, gte_one },
    { 0, 0, 0 },
};

// Divides `v` by 2^gte_frac_bits, rounding half away from zero. Integer division
// truncates toward zero, so adding +-half before it gives the rounding.
inline constexpr std::int64_t rv_gte_round_frac(std::int64_t v)
{
    const std::int64_t half = std::int64_t{ 1 } << (gte_frac_bits - 1);
    const std::int64_t biased = v < 0 ? v - half : v + half;
    return biased / (std::int64_t{ 1 } << gte_frac_bits);
}

// True when `v` fits the integer type T.
template <typename T>
inline constexpr bool rv_gte_fits(std::int64_t v)
{
    return v >= std::numeric_limits<T>::min() && v <= std::numeric_limits<T>::max();
}

// out = outer * inner: r = R_o * R_i in 4.12, t = (R_o * t_i) rounded to Q20.12 + t_o.
// Returns RV_ERR_INVAL when an r element leaves int16 or a t element leaves int32; `out` is then untouched.
[[nodiscard]] inline int rv_gte_compose(const rv_gte_matrix &outer, const rv_gte_matrix &inner, rv_gte_matrix &out)
{
    rv_gte_matrix res{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::int64_t acc = 0;
            for (int k = 0; k < 3; ++k) {
                acc += static_cast<std::int64_t>(outer.r[i * 3 + k]) * inner.r[k * 3 + j];
            }
            const std::int64_t e = rv_gte_round_frac(acc);
            if (!rv_gte_fits<std::int16_t>(e)) {
                return RV_ERR_INVAL;
            }
            res.r[i * 3 + j] = static_cast<std::int16_t>(e);
        }
    }
    for (int i = 0; i < 3; ++i) {
        std::int64_t acc = 0;
        for (int k = 0; k < 3; ++k) {
            acc += static_cast<std::int64_t>(outer.r[i * 3 + k]) * inner.t[k];
        }
        const std::int64_t e = rv_gte_round_frac(acc) + outer.t[i];
        if (!rv_gte_fits<std::int32_t>(e)) {
            return RV_ERR_INVAL;
        }
        res.t[i] = static_cast<std::int32_t>(e);
    }
    out = res;
    return RV_OK;
}

// out = m * in + t for a Q20.12 point; the product is rounded back to Q20.12.
// Returns RV_ERR_INVAL when a component leaves int32; `out` is then untouched.
[[nodiscard]] inline int rv_gte_point(const rv_gte_matrix &m, const std::int32_t in[3], std::int32_t out[3])
{
    std::int32_t res[3]{};
    for (int i = 0; i < 3; ++i) {
        std::int64_t acc = 0;
        for (int k = 0; k < 3; ++k) {
            acc += static_cast<std::int64_t>(m.r[i * 3 + k]) * in[k];
        }
        const std::int64_t e = rv_gte_round_frac(acc) + m.t[i];
        if (!rv_gte_fits<std::int32_t>(e)) {
            return RV_ERR_INVAL;
        }
        res[i] = static_cast<std::int32_t>(e);
    }
    out[0] = res[0];
    out[1] = res[1];
    out[2] = res[2];
    return RV_OK;
}

// out = ((m * v) >> shift) + t, shift in 0..gte_frac_bits. The shift floors toward minus infinity:
// no rounding, unlike the 12-shifts above.
// Returns RV_ERR_INVAL for a shift out of range or a component that leaves int32; `out` is then untouched.
[[nodiscard]] inline int rv_gte_vertex(const rv_gte_matrix &m, const std::int16_t v[3], int shift, std::int32_t out[3])
{
    if (shift < 0 || shift > gte_frac_bits) {
        return RV_ERR_INVAL;
    }
    std::int32_t res[3]{};
    for (int i = 0; i < 3; ++i) {
        std::int64_t acc = 0;
        for (int k = 0; k < 3; ++k) {
            acc += static_cast<std::int64_t>(m.r[i * 3 + k]) * v[k];
        }
        const std::int64_t e = (acc >> shift) + m.t[i];
        if (!rv_gte_fits<std::int32_t>(e)) {
            return RV_ERR_INVAL;
        }
        res[i] = static_cast<std::int32_t>(e);
    }
    out[0] = res[0];
    out[1] = res[1];
    out[2] = res[2];
    return RV_OK;
}

} // namespace rv_pdklib
