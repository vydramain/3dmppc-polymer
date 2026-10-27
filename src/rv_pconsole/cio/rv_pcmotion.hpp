// Pad motion fusion: a Mahony filter (proportional part only) that merges gyro and
// accel into the pad-to-world orientation quaternion. Axes are the console's:
// +x right, +y up, +z forward. Header-only, no state beyond rv_pcmotion.
#pragma once

#include <cmath>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_math.hpp"

namespace rv_3dmppc
{

// Accel correction gain. A gyro bias b settles as a tilt error b / KP and decays in
// about 1 / KP s. There is no integral term, so yaw is never corrected by gravity.
inline constexpr float RV_PCMOTION_KP = 1.0f;

// World up. At rest the accel (specific force) points along it, rotated into the pad frame.
inline constexpr rv_pdklib::rv_vec3 RV_PCMOTION_UP{ 0.0f, 1.0f, 0.0f };

// 1 + dot(a, RV_PCMOTION_UP), with a the normalized accel, below this means the pad is
// upside down: the shortest rotation is then a half turn, and its axis comes from the x axis.
inline constexpr float RV_PCMOTION_FLIP_EPS = 1e-6f;

struct rv_pcmotion {
    // Maps pad-frame vectors to world; conj(quat) maps world to pad.
    rv_pdklib::rv_vec4 quat{ 0.0f, 0.0f, 0.0f, 1.0f };
    // False until the first accel sample sets the tilt from gravity.
    bool primed = false;
};

namespace detail
{

// Hamilton product, (x, y, z, w) layout.
inline rv_pdklib::rv_vec4 rv_pcmotion_mul(rv_pdklib::rv_vec4 a, rv_pdklib::rv_vec4 b)
{
    return rv_pdklib::rv_vec4{ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

inline rv_pdklib::rv_vec4 rv_pcmotion_conj(rv_pdklib::rv_vec4 q)
{
    return rv_pdklib::rv_vec4{ -q.x, -q.y, -q.z, q.w };
}

// v rotated by unit quaternion q.
inline rv_pdklib::rv_vec3 rv_pcmotion_rotate(rv_pdklib::rv_vec4 q, rv_pdklib::rv_vec3 v)
{
    return rv_pdklib::rv_xyz(rv_pcmotion_mul(rv_pcmotion_mul(q, rv_pdklib::rv_direction4(v)), rv_pcmotion_conj(q)));
}

inline bool rv_pcmotion_finite(rv_pdklib::rv_vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Shortest rotation taking the unit vector `from` to RV_PCMOTION_UP.
inline rv_pdklib::rv_vec4 rv_pcmotion_align_up(rv_pdklib::rv_vec3 from)
{
    const rv_pdklib::rv_vec3 x_axis{ 1.0f, 0.0f, 0.0f };
    const float cos_angle = rv_pdklib::rv_dot(from, RV_PCMOTION_UP);
    const bool flipped = 1.0f + cos_angle < RV_PCMOTION_FLIP_EPS;
    const rv_pdklib::rv_vec3 axis = flipped ? rv_pdklib::rv_cross(from, x_axis) : rv_pdklib::rv_cross(from, RV_PCMOTION_UP);
    const float w = flipped ? 0.0f : 1.0f + cos_angle;
    const rv_pdklib::rv_vec4 q{ axis.x, axis.y, axis.z, w };
    return q * (1.0f / std::sqrt(rv_pdklib::rv_dot(q, q)));
}

} // namespace detail

// One sample. gyro in rad/s, accel in m/s^2 (gravity included), dt in s.
// Invalid input (non-finite, dt <= 0, accel length overflowing) and a state update that
// overflows return RV_ERR_INVAL and leave `fusion` untouched.
// The step is first order: it turns short of the exact angle by about theta^2 / 12 relative,
// theta = |omega| dt. For theta <= 0.1 rad that is under 0.1 %; at 248 Hz and 10 rad/s theta is 0.04.
inline int rv_pcmotion_step(rv_pcmotion &fusion, rv_pdklib::rv_vec3 gyro, rv_pdklib::rv_vec3 accel, float dt)
{
    // rv_length of an overflowing or NaN accel is non-finite, so it covers accel too.
    const bool dt_ok = dt > 0.0f && std::isfinite(dt);
    const bool gyro_ok = detail::rv_pcmotion_finite(gyro);
    const bool accel_ok = std::isfinite(rv_pdklib::rv_length(accel));
    const bool valid = dt_ok && gyro_ok && accel_ok;
    if (!valid) {
        RV_LOG_WARN("pcmotion",
            "sample refused: dt {} s (need finite and > 0), gyro finite {}, accel length finite {}",
            dt,
            gyro_ok,
            accel_ok);
        return RV_ERR_INVAL;
    }

    // Zero accel: the sensor has not sent data yet, nothing to align to.
    const rv_pdklib::rv_vec3 a = rv_pdklib::rv_normalize(accel);
    const bool has_accel = rv_pdklib::rv_dot(a, a) > 0.0f;
    if (!fusion.primed && !has_accel) {
        return RV_OK;
    }

    // Priming sets the tilt from gravity and skips integration (step 0), so base is the aligned quaternion.
    const bool prime_now = !fusion.primed;
    const rv_pdklib::rv_vec4 base = prime_now ? detail::rv_pcmotion_align_up(a) : fusion.quat;
    const float step = prime_now ? 0.0f : dt;

    // Zero accel normalizes to zero, so the error vanishes and only gyro integrates.
    // The priming step ignores gyro: a huge finite value could overflow in spin, and inf * 0 is NaN.
    const rv_pdklib::rv_vec3 estimate = detail::rv_pcmotion_rotate(detail::rv_pcmotion_conj(base), RV_PCMOTION_UP);
    const rv_pdklib::rv_vec3 error = rv_pdklib::rv_cross(a, estimate);
    const rv_pdklib::rv_vec3 rate = prime_now ? rv_pdklib::rv_vec3{} : gyro;
    const rv_pdklib::rv_vec3 omega = rate + RV_PCMOTION_KP * error;
    const rv_pdklib::rv_vec4 spin = detail::rv_pcmotion_mul(base, rv_pdklib::rv_direction4(omega));
    const rv_pdklib::rv_vec4 next = base + spin * (0.5f * step);

    // NaN or overflow in any component makes the squared norm non-finite or zero.
    const float norm2 = rv_pdklib::rv_dot(next, next);
    if (!std::isfinite(norm2) || norm2 <= 0.0f) {
        RV_LOG_WARN("pcmotion",
            "sample refused: quaternion update non-finite or zero, squared norm {} (need finite and > 0)",
            norm2);
        return RV_ERR_INVAL;
    }
    // The aligned base is already unit length: scale 1 keeps it bit-exact on the priming step.
    const float scale = prime_now ? 1.0f : 1.0f / std::sqrt(norm2);
    fusion.quat = next * scale;
    fusion.primed = true;
    return RV_OK;
}

} // namespace rv_3dmppc
