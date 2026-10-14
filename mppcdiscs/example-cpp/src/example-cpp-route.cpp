#include "example-cpp-route.hpp"

#include <cstddef>
#include <numbers>
#include <string_view>

namespace example_cpp
{

namespace
{

using rv_pdklib::fx;
using rv_pdklib::fx_vec3;

// Lens and clip planes; the far plane is a margin past the scene's extent.
constexpr float RV_ROUTE_FOV_Y = 60.0f * std::numbers::pi_v<float> / 180.0f;
constexpr float RV_ROUTE_NEAR = 0.1f;
constexpr float RV_ROUTE_FAR = 100.0f;
constexpr rv_cg_vec3 RV_ROUTE_UP = { 0.0f, 1.0f, 0.0f };

// A Catmull-Rom segment is drawn by four keys: p0 before it, p1 and p2 at its ends, p3 after it.
constexpr int64_t RV_ROUTE_SEGMENT_KEYS = 4;

// Q20.12 constants of the Catmull-Rom segment.
constexpr fx RV_ROUTE_ZERO = 0;
constexpr fx RV_ROUTE_ONE = rv_pdklib::fx_one;
constexpr fx RV_ROUTE_HALF = rv_pdklib::fx_one / 2;
constexpr fx RV_ROUTE_TWO = 2 * rv_pdklib::fx_one;
constexpr fx RV_ROUTE_THREE = 3 * rv_pdklib::fx_one;
constexpr fx RV_ROUTE_FOUR = 4 * rv_pdklib::fx_one;
constexpr fx RV_ROUTE_FIVE = 5 * rv_pdklib::fx_one;
constexpr fx RV_ROUTE_NEG_ONE = -RV_ROUTE_ONE;
constexpr fx RV_ROUTE_NEG_THREE = -RV_ROUTE_THREE;
constexpr fx RV_ROUTE_NEG_FIVE = -RV_ROUTE_FIVE;
// Weights of the keys p0..p3 in: 2 p1 (base), p2 - p0 (slope), 2 p0 - 5 p1 + 4 p2 - p3 (curve),
// -p0 + 3 p1 - 3 p2 + p3 (cubic).
constexpr fx RV_ROUTE_BASE_W[RV_ROUTE_SEGMENT_KEYS] = { RV_ROUTE_ZERO, RV_ROUTE_TWO, RV_ROUTE_ZERO, RV_ROUTE_ZERO };
constexpr fx RV_ROUTE_SLOPE_W[RV_ROUTE_SEGMENT_KEYS] = { RV_ROUTE_NEG_ONE, RV_ROUTE_ZERO, RV_ROUTE_ONE, RV_ROUTE_ZERO };
constexpr fx RV_ROUTE_CURVE_W[RV_ROUTE_SEGMENT_KEYS] = { RV_ROUTE_TWO, RV_ROUTE_NEG_FIVE, RV_ROUTE_FOUR, RV_ROUTE_NEG_ONE };
constexpr fx RV_ROUTE_CUBIC_W[RV_ROUTE_SEGMENT_KEYS] = { RV_ROUTE_NEG_ONE, RV_ROUTE_THREE, RV_ROUTE_NEG_THREE, RV_ROUTE_ONE };

// Raw steps are 1/4096 and the length is rounded down, so only a zero vector has length below one step.
// The camera then looks down +z.
constexpr fx RV_ROUTE_MIN_LENGTH = 1;
constexpr fx_vec3 RV_ROUTE_FALLBACK_DIR = { RV_ROUTE_ZERO, RV_ROUTE_ZERO, RV_ROUTE_ONE };

// The cam_NN name is "cam_" and exactly two digits.
constexpr std::string_view RV_ROUTE_KEY_PREFIX = "cam_";
constexpr std::size_t RV_ROUTE_KEY_DIGITS = 2;
constexpr std::size_t RV_ROUTE_KEY_NAME_SIZE = RV_ROUTE_KEY_PREFIX.size() + RV_ROUTE_KEY_DIGITS;
constexpr int32_t RV_ROUTE_DECIMAL_BASE = 10;

// Segment of the frame: seg runs from key seg to key seg+1 and rem / PERIOD is the frame's share of it.
// The split is exact in integers; t is rounded to Q20.12 only where fx math uses it.
void split_frame(int64_t count, int64_t frame, int64_t &seg, int64_t &rem)
{
    const int64_t phase = ((frame % RV_ROUTE_PERIOD_FRAMES) + RV_ROUTE_PERIOD_FRAMES) % RV_ROUTE_PERIOD_FRAMES;
    const int64_t scaled = phase * count;
    seg = scaled / RV_ROUTE_PERIOD_FRAMES;
    rem = scaled % RV_ROUTE_PERIOD_FRAMES;
}

// w0 p0 + w1 p1 + w2 p2 + w3 p3 in Q20.12, each term and each sum checked. Error: out is untouched.
int weighted_sum(const fx_vec3 (&p)[RV_ROUTE_SEGMENT_KEYS], const fx (&w)[RV_ROUTE_SEGMENT_KEYS], fx_vec3 &out)
{
    fx_vec3 term[RV_ROUTE_SEGMENT_KEYS] = {};
    for (int64_t i = 0; i < RV_ROUTE_SEGMENT_KEYS; ++i) {
        if (rv_pdklib::fx_vec3_scale(p[i], w[i], term[i]) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    fx_vec3 low = {};
    if (rv_pdklib::fx_vec3_add(term[0], term[1], low) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 high = {};
    if (rv_pdklib::fx_vec3_add(term[2], term[3], high) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 sum = {};
    if (rv_pdklib::fx_vec3_add(low, high, sum) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = sum;
    return RV_OK;
}

// Uniform Catmull-Rom point between p[1] and p[2] at t in [0, 1]:
// 0.5 * (2 p1 + slope t + curve t^2 + cubic t^3). Error: out is untouched.
int spline(const fx_vec3 (&p)[RV_ROUTE_SEGMENT_KEYS], fx t, fx_vec3 &out)
{
    fx t2 = 0;
    if (rv_pdklib::fx_mul(t, t, t2) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx t3 = 0;
    if (rv_pdklib::fx_mul(t2, t, t3) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 base = {};
    if (weighted_sum(p, RV_ROUTE_BASE_W, base) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 slope = {};
    if (weighted_sum(p, RV_ROUTE_SLOPE_W, slope) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 curve = {};
    if (weighted_sum(p, RV_ROUTE_CURVE_W, curve) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 cubic = {};
    if (weighted_sum(p, RV_ROUTE_CUBIC_W, cubic) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 slope_t = {};
    if (rv_pdklib::fx_vec3_scale(slope, t, slope_t) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 curve_t2 = {};
    if (rv_pdklib::fx_vec3_scale(curve, t2, curve_t2) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 cubic_t3 = {};
    if (rv_pdklib::fx_vec3_scale(cubic, t3, cubic_t3) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 head = {};
    if (rv_pdklib::fx_vec3_add(base, slope_t, head) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 tail = {};
    if (rv_pdklib::fx_vec3_add(curve_t2, cubic_t3, tail) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 sum = {};
    if (rv_pdklib::fx_vec3_add(head, tail, sum) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 result = {};
    if (rv_pdklib::fx_vec3_scale(sum, RV_ROUTE_HALF, result) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = result;
    return RV_OK;
}

// Spline of the field (eye or dir) of the frame's segment at the frame's share t. Error: out is untouched.
int route_spline(const std::vector<rv_route_key> &keys, int64_t frame, fx_vec3 rv_route_key::*field, fx_vec3 &out)
{
    if (keys.empty()) {
        return RV_ERR_NOENT;
    }
    const int64_t count = static_cast<int64_t>(keys.size());
    int64_t seg = 0;
    int64_t rem = 0;
    split_frame(count, frame, seg, rem);
    fx t = 0;
    if (rv_pdklib::fx_div(static_cast<fx>(rem), static_cast<fx>(RV_ROUTE_PERIOD_FRAMES), t) != RV_OK) {
        return RV_ERR_INVAL;
    }

    // The route is closed: the keys wrap around, so the last segment ends on key 0.
    fx_vec3 pts[RV_ROUTE_SEGMENT_KEYS] = {};
    for (int64_t i = 0; i < RV_ROUTE_SEGMENT_KEYS; ++i) {
        pts[i] = keys[(seg + count - 1 + i) % count].*field;
    }
    return spline(pts, t, out);
}

rv_cg_vec3 to_float(fx_vec3 v)
{
    return { rv_pdklib::fx_to_float(v.x), rv_pdklib::fx_to_float(v.y), rv_pdklib::fx_to_float(v.z) };
}

rv_cg_camera make_camera(const rv_cg_vec3 &eye, const rv_cg_vec3 &target)
{
    rv_cg_camera camera = {};
    camera.eye = eye;
    camera.target = target;
    camera.up = RV_ROUTE_UP;
    camera.fov_y = RV_ROUTE_FOV_Y;
    camera.near_plane = RV_ROUTE_NEAR;
    camera.far_plane = RV_ROUTE_FAR;
    return camera;
}

} // namespace

bool rv_route_key_name(const char *name, int32_t &index)
{
    const std::string_view text = name;
    if (text.size() != RV_ROUTE_KEY_NAME_SIZE || !text.starts_with(RV_ROUTE_KEY_PREFIX)) {
        return false;
    }
    const char tens = text[RV_ROUTE_KEY_PREFIX.size()];
    const char ones = text[RV_ROUTE_KEY_PREFIX.size() + 1];
    if (tens < '0' || tens > '9' || ones < '0' || ones > '9') {
        return false;
    }
    index = (tens - '0') * RV_ROUTE_DECIMAL_BASE + (ones - '0');
    return true;
}

int rv_route_unit(fx_vec3 v, fx_vec3 &out)
{
    fx length = 0;
    if (rv_pdklib::fx_vec3_length(v, length) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (length < RV_ROUTE_MIN_LENGTH) {
        out = RV_ROUTE_FALLBACK_DIR;
        return RV_OK;
    }
    fx_vec3 unit = {};
    if (rv_pdklib::fx_div(v.x, length, unit.x) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_pdklib::fx_div(v.y, length, unit.y) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (rv_pdklib::fx_div(v.z, length, unit.z) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = unit;
    return RV_OK;
}

int rv_route_eye_at(const std::vector<rv_route_key> &keys, int64_t frame, fx_vec3 &out)
{
    return route_spline(keys, frame, &rv_route_key::eye, out);
}

int rv_route_camera_at(const std::vector<rv_route_key> &keys, int64_t frame, rv_cg_camera &out)
{
    fx_vec3 eye = {};
    const int eye_rv = rv_route_eye_at(keys, frame, eye);
    if (eye_rv != RV_OK) {
        return eye_rv;
    }
    fx_vec3 dir_curve = {};
    const int dir_rv = route_spline(keys, frame, &rv_route_key::dir, dir_curve);
    if (dir_rv != RV_OK) {
        return dir_rv;
    }
    fx_vec3 dir = {};
    if (rv_route_unit(dir_curve, dir) != RV_OK) {
        return RV_ERR_INVAL;
    }
    fx_vec3 target = {};
    if (rv_pdklib::fx_vec3_add(eye, dir, target) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = make_camera(to_float(eye), to_float(target));
    return RV_OK;
}

} // namespace example_cpp
