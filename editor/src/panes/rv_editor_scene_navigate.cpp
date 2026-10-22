// The viewport navigation's operations on the camera's Q20.12 values: orbit, pan, dolly, seek and the resets. The
// viewport turns input into doubles and calls these. The Scene tools (viewport_tools.cpp) move the camera too.

#include "panes/rv_editor_scene_draw.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "app/rv_editor_app_parts.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "scene/rv_editor_scene.hpp"

namespace rv_editor
{

namespace
{

// Pitch limit 89.9 degrees: 89.9 * 4096 = 368230.4, rounded to nearest raw.
constexpr int64_t pitch_limit_raw = 368230;
// Distance limits 0.5 and 200 units, both exact in raw.
constexpr int64_t distance_min_raw = 2048;
constexpr int64_t distance_max_raw = 819200;
// One full turn in raw: yaw wraps into [0, 360) degrees.
constexpr int64_t full_turn_raw = 360 * rv_pdklib::fx_one;
// The target moves this fraction of the distance per mouse pixel.
constexpr double pan_sensitivity = 0.002;
// The target's axes: x, y, z.
constexpr size_t pan_axes = 3;
// Largest raw a delta may reach: beyond it, or NaN, the step is refused.
constexpr double fx_raw_max = static_cast<double>(std::numeric_limits<rv_pdklib::fx>::max());

// An operation that cannot apply its step says why and returns the code.
int refuse(int rc, const char *what)
{
    RV_LOG_WARN(scene_log_tag, "camera {} refused (rc {}): the view is unchanged", what, rc);
    return rc;
}

// The five Q20.12 values of the camera; the first getter that fails gives the code.
int get_all(const rv_editor_scene_camera &cam,
    rv_pdklib::fx &yaw,
    rv_pdklib::fx &pitch,
    rv_pdklib::fx &distance,
    rv_pdklib::fx_vec3 &target)
{
    if (const int rc = rv_editor_camera_get_yaw_fx(cam, yaw); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_camera_get_pitch_fx(cam, pitch); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_camera_get_distance_fx(cam, distance); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_camera_get_target_fx(cam, target); rc != RV_OK) {
        return rc;
    }
    return RV_OK;
}

// Rounds `units` plus the carry to whole raw steps; `rest` is what the rounding leaves for the next step.
// Error: the value is not finite or leaves fx; whole and rest are untouched.
int split_raw(double units, double carry, int64_t &whole, double &rest)
{
    const double steps = units * rv_pdklib::fx_one + carry;
    const bool fits = std::fabs(steps) <= fx_raw_max;
    if (!fits) {
        return RV_ERR_INVAL;
    }
    const double rounded = std::round(steps);
    whole = static_cast<int64_t>(rounded);
    rest = steps - rounded;
    return RV_OK;
}

// Yaw raw in [0, full turn), also for a negative sum.
int64_t wrap_turn(int64_t raw)
{
    const int64_t rest = raw % full_turn_raw;
    return rest < 0 ? rest + full_turn_raw : rest;
}

// Sets one value back to its home value; get and set are the value's accessors.
template <typename Get, typename Set>
int reset_axis(rv_editor_scene_camera &cam, Get get, Set set)
{
    rv_pdklib::fx home = 0;
    if (const int rc = get(rv_editor_camera_home(cam), home); rc != RV_OK) {
        return refuse(rc, "reset");
    }
    set(cam, home);
    return RV_OK;
}

} // namespace

int rv_editor_camera_check(rv_editor_scene_camera &cam)
{
    rv_pdklib::fx yaw = 0;
    rv_pdklib::fx pitch = 0;
    rv_pdklib::fx distance = 0;
    rv_pdklib::fx_vec3 target{};
    if (const int rc = get_all(cam, yaw, pitch, distance, target); rc != RV_OK) {
        RV_LOG_WARN(scene_log_tag, "viewport camera is outside Q20.12 (rc {}): the view is reset to home", rc);
        cam = rv_editor_camera_home(cam);
        return rc;
    }
    return RV_OK;
}

int rv_editor_camera_read_units(const rv_editor_scene_camera &cam, rv_editor_camera_units &out)
{
    rv_pdklib::fx yaw = 0;
    rv_pdklib::fx pitch = 0;
    rv_pdklib::fx distance = 0;
    rv_pdklib::fx_vec3 target{};
    if (const int rc = get_all(cam, yaw, pitch, distance, target); rc != RV_OK) {
        return refuse(rc, "units read");
    }
    out = rv_editor_camera_units{ rv_editor_camera_units_of(yaw),
        rv_editor_camera_units_of(pitch),
        rv_editor_camera_units_of(distance) };
    return RV_OK;
}

rv_editor_scene_camera rv_editor_camera_home(const rv_editor_scene_camera &cam)
{
    return rv_editor_scene_camera{ cam.tool, cam.snap, cam.snap_step, cam.grid, cam.shading };
}

int rv_editor_camera_orbit(rv_editor_scene_camera &cam, rv_editor_camera_carry &carry, double yaw_units, double pitch_units)
{
    rv_pdklib::fx yaw = 0;
    rv_pdklib::fx pitch = 0;
    if (const int rc = rv_editor_camera_get_yaw_fx(cam, yaw); rc != RV_OK) {
        return refuse(rc, "orbit");
    }
    if (const int rc = rv_editor_camera_get_pitch_fx(cam, pitch); rc != RV_OK) {
        return refuse(rc, "orbit");
    }
    int64_t yaw_whole = 0;
    int64_t pitch_whole = 0;
    double yaw_rest = 0.0;
    double pitch_rest = 0.0;
    if (const int rc = split_raw(yaw_units, carry.yaw, yaw_whole, yaw_rest); rc != RV_OK) {
        return refuse(rc, "orbit");
    }
    if (const int rc = split_raw(pitch_units, carry.pitch, pitch_whole, pitch_rest); rc != RV_OK) {
        return refuse(rc, "orbit");
    }
    const int64_t yaw_next = wrap_turn(static_cast<int64_t>(yaw) + yaw_whole);
    const int64_t pitch_next = std::clamp(static_cast<int64_t>(pitch) + pitch_whole, -pitch_limit_raw, pitch_limit_raw);
    rv_editor_camera_set_yaw_fx(cam, static_cast<rv_pdklib::fx>(yaw_next));
    rv_editor_camera_set_pitch_fx(cam, static_cast<rv_pdklib::fx>(pitch_next));
    carry.yaw = yaw_rest;
    carry.pitch = pitch_rest;
    return RV_OK;
}

// The factor exp(steps) rounds to raw once: its error is at most half a raw, 2^-13 of the factor, so the relative
// error is 2^-13 / factor. A factor that rounds to 1.0 is not applied and its steps stay in the carry. An applied
// factor leaves the ln the rounding dropped in the carry. A factor or product outside Q20.12 clamps to the distance
// limit: the normal limit, not an error.
int rv_editor_camera_dolly(rv_editor_scene_camera &cam, rv_editor_camera_carry &carry, double log_factor)
{
    rv_pdklib::fx distance = 0;
    if (const int rc = rv_editor_camera_get_distance_fx(cam, distance); rc != RV_OK) {
        return refuse(rc, "dolly");
    }
    const double steps = log_factor + carry.dolly;
    if (!std::isfinite(steps)) {
        return refuse(RV_ERR_INVAL, "dolly");
    }
    const double raw_factor = std::round(std::exp(steps) * rv_pdklib::fx_one);
    const bool applied = raw_factor != rv_pdklib::fx_one;
    const bool factor_fits = raw_factor > 0.0 && raw_factor <= fx_raw_max;
    const int64_t factor = static_cast<int64_t>(std::clamp(raw_factor, 0.0, fx_raw_max));
    const int64_t product = static_cast<int64_t>(distance) * factor;
    const int64_t scaled = (product + rv_pdklib::fx_one / 2) / rv_pdklib::fx_one;
    const int64_t clamped = std::clamp(scaled, distance_min_raw, distance_max_raw);
    rv_editor_camera_set_distance_fx(cam, static_cast<rv_pdklib::fx>(clamped));
    carry.dolly = steps;
    if (applied && factor_fits) {
        carry.dolly = steps - std::log(raw_factor / rv_pdklib::fx_one);
    } else if (applied) {
        carry.dolly = 0.0;
    }
    return RV_OK;
}

int rv_editor_camera_pan(rv_editor_scene_camera &cam,
    rv_editor_camera_carry &carry,
    const rv_editor_view &v,
    double dx,
    double dy)
{
    rv_pdklib::fx distance = 0;
    if (const int rc = rv_editor_camera_get_distance_fx(cam, distance); rc != RV_OK) {
        return refuse(rc, "pan");
    }
    rv_pdklib::fx_vec3 target{};
    if (const int rc = rv_editor_camera_get_target_fx(cam, target); rc != RV_OK) {
        return refuse(rc, "pan");
    }
    const double k = rv_editor_camera_units_of(distance) * pan_sensitivity;
    const vec3 delta = add(mul(v.right, -dx * k), mul(v.up, dy * k));
    const std::array<rv_pdklib::fx, pan_axes> start{ target.x, target.y, target.z };
    std::array<rv_pdklib::fx, pan_axes> next{};
    std::array<double, pan_axes> rest{};
    for (size_t a = 0; a < pan_axes; ++a) {
        int64_t whole = 0;
        if (const int rc = split_raw(delta[a], carry.pan[a], whole, rest[a]); rc != RV_OK) {
            return refuse(rc, "pan");
        }
        if (const int rc = rv_pdklib::fx_from_raw(static_cast<int64_t>(start[a]) + whole, next[a]); rc != RV_OK) {
            return refuse(rc, "pan");
        }
    }
    rv_editor_camera_set_target_fx(cam, rv_pdklib::fx_vec3{ next[0], next[1], next[2] });
    carry.pan = rest;
    return RV_OK;
}

int rv_editor_camera_seek(rv_editor_scene_camera &cam, const std::array<double, 3> &point)
{
    std::array<rv_pdklib::fx, pan_axes> axes{};
    for (size_t a = 0; a < pan_axes; ++a) {
        if (const int rc = rv_pdklib::fx_from_double(point[a], axes[a]); rc != RV_OK) {
            return refuse(rc, "seek");
        }
    }
    rv_editor_camera_set_target_fx(cam, rv_pdklib::fx_vec3{ axes[0], axes[1], axes[2] });
    return RV_OK;
}

int rv_editor_camera_reset_yaw(rv_editor_scene_camera &cam)
{
    return reset_axis(cam, rv_editor_camera_get_yaw_fx, rv_editor_camera_set_yaw_fx);
}

int rv_editor_camera_reset_pitch(rv_editor_scene_camera &cam)
{
    return reset_axis(cam, rv_editor_camera_get_pitch_fx, rv_editor_camera_set_pitch_fx);
}

int rv_editor_camera_reset_distance(rv_editor_scene_camera &cam)
{
    return reset_axis(cam, rv_editor_camera_get_distance_fx, rv_editor_camera_set_distance_fx);
}

} // namespace rv_editor
