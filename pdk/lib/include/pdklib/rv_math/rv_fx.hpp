#pragma once

// Q20.12 fixed point for game math: integers only, so the console gets the same
// numbers on every run. float appears only where values cross to the PDK contract.

#include <cmath>
#include <cstdint>
#include <limits>

#include <pdk/rv_err.h>

#include "rv_math.hpp"

namespace rv_pdklib
{

using fx = int32_t;

// Shift of the fraction: 12 bits, one unit is 4096 raw steps (step 1/4096).
constexpr int fx_shift = 12;
// 1.0 in raw form: 1 << fx_shift.
constexpr fx fx_one = 1 << fx_shift;
// Whole-unit range accepted by fx_from_units (its int64_t input counts units, not raw
// steps): [-2^19, 2^19 - 1]. The raw range is all of int32.
constexpr int64_t fx_int_min = -524288;
constexpr int64_t fx_int_max = 524287;
// Highest power of four in uint64; the bit method starts from it.
constexpr int fx_isqrt_top_bit = 62;

// Internal: true when v is a raw value of fx.
inline bool fx_fits(int64_t v)
{
    return v >= std::numeric_limits<fx>::min() && v <= std::numeric_limits<fx>::max();
}

// Units to fx, nearest raw value, half away from zero. NaN and inf fail the range
// test (every comparison with NaN is false). Error: the value leaves fx; out is untouched.
inline int fx_from_double(double v, fx &out)
{
    const double raw = std::round(v * fx_one);
    const bool fits = raw >= static_cast<double>(std::numeric_limits<fx>::min()) &&
        raw <= static_cast<double>(std::numeric_limits<fx>::max());
    if (!fits) {
        return RV_ERR_INVAL;
    }
    out = static_cast<fx>(raw);
    return RV_OK;
}

// Whole units to fx. Error: n is outside [fx_int_min, fx_int_max]; out is untouched.
inline int fx_from_units(int64_t n, fx &out)
{
    const bool fits = n >= fx_int_min && n <= fx_int_max;
    if (!fits) {
        return RV_ERR_INVAL;
    }
    out = static_cast<fx>(n * fx_one);
    return RV_OK;
}

// Raw value stored in an integer. Error: q leaves fx; out is untouched.
inline int fx_from_raw(int64_t q, fx &out)
{
    if (!fx_fits(q)) {
        return RV_ERR_INVAL;
    }
    out = static_cast<fx>(q);
    return RV_OK;
}

// Internal: p / 4096 rounded to nearest, half away from zero. Any int64 p is safe:
// the magnitude is taken in uint64.
inline int64_t fx_round_raw(int64_t p)
{
    const uint64_t mag = p < 0 ? uint64_t{ 0 } - static_cast<uint64_t>(p) : static_cast<uint64_t>(p);
    const uint64_t whole = mag >> fx_shift;
    const uint64_t rest = mag & (static_cast<uint64_t>(fx_one) - 1);
    const uint64_t r = whole + static_cast<uint64_t>(rest >= static_cast<uint64_t>(fx_one) / 2);
    return p < 0 ? -static_cast<int64_t>(r) : static_cast<int64_t>(r);
}

// Internal: a + b in int64; false (sum untouched) when the sum would leave int64.
inline bool fx_add_i64(int64_t a, int64_t b, int64_t &sum)
{
    const bool up = b > 0 && a > std::numeric_limits<int64_t>::max() - b;
    const bool down = b < 0 && a < std::numeric_limits<int64_t>::min() - b;
    if (up || down) {
        return false;
    }
    sum = a + b;
    return true;
}

// Internal: raw a * b at scale 2^24; |a * b| <= 2^62, exact in int64.
inline int64_t fx_mul_raw(fx a, fx b)
{
    return static_cast<int64_t>(a) * b;
}

// Error: the result leaves fx; out is untouched.
inline int fx_mul(fx a, fx b, fx &out)
{
    const int64_t r = fx_round_raw(fx_mul_raw(a, b));
    if (!fx_fits(r)) {
        return RV_ERR_INVAL;
    }
    out = static_cast<fx>(r);
    return RV_OK;
}

// Nearest raw quotient, half away from zero. Error: b == 0 or the result leaves fx;
// out is untouched.
inline int fx_div(fx a, fx b, fx &out)
{
    if (b == 0) {
        return RV_ERR_INVAL;
    }
    const int64_t num = static_cast<int64_t>(a) * fx_one;
    const int64_t den = static_cast<int64_t>(b);
    const bool negative = (num < 0) != (den < 0);
    const int64_t n = num < 0 ? -num : num;
    const int64_t d = den < 0 ? -den : den;
    const int64_t mag = (2 * n + d) / (2 * d);
    const int64_t r = negative ? -mag : mag;
    if (!fx_fits(r)) {
        return RV_ERR_INVAL;
    }
    out = static_cast<fx>(r);
    return RV_OK;
}

// Internal: floor of the integer square root of n (bit method, integers only).
inline uint64_t fx_isqrt(uint64_t n)
{
    uint64_t x = n;
    uint64_t bit = uint64_t{ 1 } << fx_isqrt_top_bit;
    uint64_t root = 0;
    while (bit > x) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (x >= root + bit) {
            x -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

// Floor of sqrt(a). sqrt(a / 4096) * 4096 == sqrt(a * 4096), so the integer root of the
// shifted raw value is the raw root. Error: a < 0; out is untouched.
inline int fx_sqrt(fx a, fx &out)
{
    if (a < 0) {
        return RV_ERR_INVAL;
    }
    out = static_cast<fx>(fx_isqrt(static_cast<uint64_t>(a) << fx_shift));
    return RV_OK;
}

struct fx_vec3 {
    fx x, y, z;
};

// Internal: writes out only when x, y and z all fit fx; out is untouched on error.
inline int fx_vec3_store(int64_t x, int64_t y, int64_t z, fx_vec3 &out)
{
    if (!fx_fits(x) || !fx_fits(y) || !fx_fits(z)) {
        return RV_ERR_INVAL;
    }
    out = fx_vec3{ static_cast<fx>(x), static_cast<fx>(y), static_cast<fx>(z) };
    return RV_OK;
}

inline int fx_vec3_add(fx_vec3 a, fx_vec3 b, fx_vec3 &out)
{
    const int64_t x = static_cast<int64_t>(a.x) + b.x;
    const int64_t y = static_cast<int64_t>(a.y) + b.y;
    const int64_t z = static_cast<int64_t>(a.z) + b.z;
    return fx_vec3_store(x, y, z, out);
}

inline int fx_vec3_sub(fx_vec3 a, fx_vec3 b, fx_vec3 &out)
{
    const int64_t x = static_cast<int64_t>(a.x) - b.x;
    const int64_t y = static_cast<int64_t>(a.y) - b.y;
    const int64_t z = static_cast<int64_t>(a.z) - b.z;
    return fx_vec3_store(x, y, z, out);
}

inline int fx_vec3_scale(fx_vec3 v, fx s, fx_vec3 &out)
{
    const int64_t x = fx_round_raw(fx_mul_raw(v.x, s));
    const int64_t y = fx_round_raw(fx_mul_raw(v.y, s));
    const int64_t z = fx_round_raw(fx_mul_raw(v.z, s));
    return fx_vec3_store(x, y, z, out);
}

// Products are summed in int64, each partial sum checked before it is added, then
// rounded once. Error: a partial sum or the result leaves its range; out is untouched.
inline int fx_vec3_dot(fx_vec3 a, fx_vec3 b, fx &out)
{
    const int64_t px = fx_mul_raw(a.x, b.x);
    const int64_t py = fx_mul_raw(a.y, b.y);
    const int64_t pz = fx_mul_raw(a.z, b.z);
    int64_t partial = 0;
    if (!fx_add_i64(px, py, partial)) {
        return RV_ERR_INVAL;
    }
    int64_t sum = 0;
    if (!fx_add_i64(partial, pz, sum)) {
        return RV_ERR_INVAL;
    }
    const int64_t r = fx_round_raw(sum);
    if (!fx_fits(r)) {
        return RV_ERR_INVAL;
    }
    out = static_cast<fx>(r);
    return RV_OK;
}

// Internal: square of the magnitude; |a|^2 <= 2^62, so three of them never overflow uint64.
inline uint64_t fx_sq_u64(fx a)
{
    const uint64_t m = a < 0 ? static_cast<uint64_t>(-static_cast<int64_t>(a)) : static_cast<uint64_t>(a);
    return m * m;
}

// Root of the raw sum of squares (scale 2^24): sqrt(sum) == |v| * 4096, floored, with no
// rounding of an intermediate value. Error: the root leaves fx; out is untouched.
inline int fx_vec3_length(fx_vec3 v, fx &out)
{
    const uint64_t root = fx_isqrt(fx_sq_u64(v.x) + fx_sq_u64(v.y) + fx_sq_u64(v.z));
    if (root > static_cast<uint64_t>(std::numeric_limits<fx>::max())) {
        return RV_ERR_INVAL;
    }
    out = static_cast<fx>(root);
    return RV_OK;
}

// The only place fx becomes float. Exact for |v| < 4096: the raw value then has at
// most 24 significant bits, which float holds.
inline float fx_to_float(fx q)
{
    return static_cast<float>(q) / static_cast<float>(fx_one);
}

inline rv_vec3 fx_vec3_to_float(fx_vec3 v)
{
    return rv_vec3{ fx_to_float(v.x), fx_to_float(v.y), fx_to_float(v.z) };
}

} // namespace rv_pdklib
