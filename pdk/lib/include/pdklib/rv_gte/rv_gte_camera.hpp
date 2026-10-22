#pragma once

// Integer camera for rv_gte: the rv_camera_make of pdklib/rv_camera/rv_camera.hpp, in Q20.12.
// Same convention: left-handed, +z in front of the eye, view rows right / up / forward.

#include <cstdint>

#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_project.hpp"
#include "pdklib/rv_gte/rv_gte_trig.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace rv_pdklib
{

// Directions are kept in Q.24 (one = 2^24) so the cross products do not lose the bits a 4.12 round would.
inline constexpr int gte_dir_shift = 2 * gte_frac_bits;
inline constexpr std::int64_t gte_dir_one = std::int64_t{ 1 } << gte_dir_shift;

// Smallest |up x forward| in raw Q.24 accepted: 2^12 is sin 1/4096. Each direction is rounded to 2^-24 per
// component, so the right axis is off by about 2 * 2^-12 / sin units of 4.12 before the rows are rounded;
// at this minimum that is about 2 units.
inline constexpr std::int64_t gte_camera_min_side = std::int64_t{ 1 } << gte_frac_bits;

// Field of view in degrees, open at the top. The 4.12 sin of half the field has a relative error of
// 0.5 / (4096 * sin(fov / 2)): 0.14% at 10 degrees, about 2 px at h = 1372 (height 240). Below 10 degrees
// the error grows as 1 / fov, to 28 px at 1 degree, so those fields are refused.
inline constexpr std::int64_t gte_camera_fov_min_degrees = 10;
inline constexpr std::int64_t gte_camera_fov_max_degrees = 180;

// View matrix, focal length h in pixels and the two clip planes. near_plane and far_plane fill the two fx
// fields of rv_gte_depth_range; depth_min and depth_max stay with the caller.
struct rv_gte_camera {
    rv_gte_matrix view;
    std::int32_t h;
    fx near_plane;
    fx far_plane;
};

// Internal: a vector as three raw integers, Q.24 for directions and Q.48 for an unrounded cross product.
struct rv_gte_dir {
    std::int64_t x;
    std::int64_t y;
    std::int64_t z;
};

// Internal: v as a Q.24 unit vector, each component rounded half up. Error: v is zero or the
// result leaves int64; out is untouched.
[[nodiscard]] inline int rv_gte_camera_dir(fx_vec3 v, rv_gte_dir &out)
{
    fx len = 0;
    const int rc = fx_vec3_length(v, len);
    if (rc != RV_OK) {
        return rc;
    }
    if (len == 0) {
        return RV_ERR_INVAL;
    }
    out = rv_gte_dir{
        rv_gte_div_round(static_cast<std::int64_t>(v.x) * gte_dir_one, len),
        rv_gte_div_round(static_cast<std::int64_t>(v.y) * gte_dir_one, len),
        rv_gte_div_round(static_cast<std::int64_t>(v.z) * gte_dir_one, len),
    };
    return RV_OK;
}

// Internal: a x b without rounding; the scale is the product of the two input scales.
inline rv_gte_dir rv_gte_camera_cross(rv_gte_dir a, rv_gte_dir b)
{
    return rv_gte_dir{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

// Internal: each component divided by den, rounded half up. den > 0.
inline rv_gte_dir rv_gte_camera_round(rv_gte_dir v, std::int64_t den)
{
    return rv_gte_dir{ rv_gte_div_round(v.x, den), rv_gte_div_round(v.y, den), rv_gte_div_round(v.z, den) };
}

// Internal: v squared, in uint64; the sign is taken away first, as fx_sq_u64 does in rv_fx.hpp.
inline std::uint64_t rv_gte_camera_sq(std::int64_t v)
{
    const auto m = static_cast<std::uint64_t>(v < 0 ? -v : v);
    return m * m;
}

// Internal: floor of the length of a Q.24 vector, in Q.24. Components are unit-vector values, at most
// 2^24 + 1 in magnitude, so each square is at most about 2^48 and the sum fits uint64.
inline std::uint64_t rv_gte_camera_length(rv_gte_dir v)
{
    return fx_isqrt(rv_gte_camera_sq(v.x) + rv_gte_camera_sq(v.y) + rv_gte_camera_sq(v.z));
}

// Internal: sin and cos of half a field of view (raw Q20.12 degrees), both scaled by 4096 * 4096. Each table
// step is 360 / 4096 degrees: without interpolation the focal length is off by up to about 5 px at 20 degrees
// and a 240 px height. Linear interpolation between the two neighbouring steps removes the step error; the
// 4.12 table values keep their own rounding.
inline void rv_gte_camera_half_sincos(fx degrees, std::int64_t &sin_num, std::int64_t &cos_num)
{
    const auto index = static_cast<std::int32_t>(degrees / gte_degrees_per_turn);
    const std::int64_t frac = (degrees % gte_degrees_per_turn) * fx_one / gte_degrees_per_turn;
    const std::int64_t lo = fx_one - frac;
    sin_num = rv_gte_sin(index) * lo + rv_gte_sin(index + 1) * frac;
    cos_num = rv_gte_cos(index) * lo + rv_gte_cos(index + 1) * frac;
}

// Internal: h = (height / 2) / tan(fov / 2) = height * cos / (2 * sin), rounded half up. Error: fov outside
// [gte_camera_fov_min_degrees, gte_camera_fov_max_degrees), or h < 1 (the field is too wide for the height);
// h is untouched. Accuracy against the rv_camera_make reference at height 240, measured over 10^4 random cameras:
// h within 1 px for fov 20..170 degrees (max 0.78 px); 15..20 gives up to 1.15 px, 12..15 up to 1.60 px,
// 10..12 up to 2.10 px.
[[nodiscard]] inline int rv_gte_camera_focal(fx fov_y_degrees, std::int32_t screen_height, std::int32_t &h)
{
    const bool above_min = fov_y_degrees >= gte_camera_fov_min_degrees * fx_one;
    const bool below_max = fov_y_degrees < gte_camera_fov_max_degrees * fx_one;
    if (!above_min || !below_max) {
        return RV_ERR_INVAL;
    }
    std::int64_t sin_num = 0;
    std::int64_t cos_num = 0;
    rv_gte_camera_half_sincos(fov_y_degrees / 2, sin_num, cos_num);
    const std::int64_t value = rv_gte_div_round(static_cast<std::int64_t>(screen_height) * cos_num, 2 * sin_num);
    if (value < 1 || !rv_gte_fits<std::int32_t>(value)) {
        return RV_ERR_INVAL;
    }
    h = static_cast<std::int32_t>(value);
    return RV_OK;
}

// Builds the camera from a left-handed look-at (eye, target, up), the full vertical field of view in degrees,
// the screen height in pixels and the clip planes. out is written only on RV_OK.
// Error: RV_ERR_INVAL for screen_height <= 0, near <= 0 or far <= near, a fov or h outside the rv_gte_camera_focal
// range, eye == target, up zero or parallel to the aim (|up x forward| below gte_camera_min_side), or a value
// that leaves its range.
[[nodiscard]] inline int rv_gte_camera_make(fx_vec3 eye,
    fx_vec3 target,
    fx_vec3 up,
    fx fov_y_degrees,
    std::int32_t screen_height,
    fx near_plane,
    fx far_plane,
    rv_gte_camera &out)
{
    if (screen_height <= 0) {
        return RV_ERR_INVAL;
    }
    if (near_plane <= 0 || far_plane <= near_plane) {
        return RV_ERR_INVAL;
    }
    std::int32_t h = 0;
    int rc = rv_gte_camera_focal(fov_y_degrees, screen_height, h);
    if (rc != RV_OK) {
        return rc;
    }
    fx_vec3 aim{};
    rc = fx_vec3_sub(target, eye, aim);
    if (rc != RV_OK) {
        return rc;
    }
    rv_gte_dir forward{};
    rc = rv_gte_camera_dir(aim, forward);
    if (rc != RV_OK) {
        return rc;
    }
    rv_gte_dir up_dir{};
    rc = rv_gte_camera_dir(up, up_dir);
    if (rc != RV_OK) {
        return rc;
    }
    // Side = up x forward, kept in Q.24 so that a near-parallel pair still gives a precise right axis.
    const rv_gte_dir side = rv_gte_camera_round(rv_gte_camera_cross(up_dir, forward), gte_dir_one);
    const auto side_len = static_cast<std::int64_t>(rv_gte_camera_length(side));
    if (side_len < gte_camera_min_side) {
        return RV_ERR_INVAL;
    }
    // Right in 4.12: side / |side|, with the Q.24 length as the divisor.
    const rv_gte_dir right{
        rv_gte_div_round(side.x * fx_one, side_len),
        rv_gte_div_round(side.y * fx_one, side_len),
        rv_gte_div_round(side.z * fx_one, side_len),
    };
    // Upward = forward x right in Q.24 units of both; the Q.48 product is rounded once to 4.12 (2^36).
    const rv_gte_dir right_q24{ right.x * fx_one, right.y * fx_one, right.z * fx_one };
    const rv_gte_dir upward =
        rv_gte_camera_round(rv_gte_camera_cross(forward, right_q24), std::int64_t{ 1 } << (gte_dir_shift + gte_frac_bits));
    const rv_gte_dir forward_q12 = rv_gte_camera_round(forward, fx_one);

    // Rows are the camera axes; t = -row . eye uses the rounded 4.12 rows, so the eye maps to 0.
    const rv_gte_dir rows[3] = { right, upward, forward_q12 };
    const std::int64_t eye_c[3] = { eye.x, eye.y, eye.z };
    rv_gte_matrix view{};
    for (int i = 0; i < 3; ++i) {
        view.r[i * 3 + 0] = static_cast<std::int16_t>(rows[i].x);
        view.r[i * 3 + 1] = static_cast<std::int16_t>(rows[i].y);
        view.r[i * 3 + 2] = static_cast<std::int16_t>(rows[i].z);
        std::int64_t acc = 0;
        for (int k = 0; k < 3; ++k) {
            acc += static_cast<std::int64_t>(view.r[i * 3 + k]) * eye_c[k];
        }
        const std::int64_t t = -rv_gte_round_frac(acc);
        if (!rv_gte_fits<std::int32_t>(t)) {
            return RV_ERR_INVAL;
        }
        view.t[i] = static_cast<std::int32_t>(t);
    }
    out = rv_gte_camera{ view, h, near_plane, far_plane };
    return RV_OK;
}

} // namespace rv_pdklib
