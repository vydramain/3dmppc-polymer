#include "example-cpp-pdkkit-route.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <string_view>
#include <vector>

#include "pdk/cv/rv_cv.h"
#include "pdklib/rv_gte/rv_gte_camera.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace example_cpp_pdkkit
{

namespace
{

using rv_pdklib::fx;
using rv_pdklib::fx_vec3;

// Lens and clip planes in Q20.12; the far plane is a margin past the scene's extent. The field is in degrees.
// The near plane is 0.1 m = 409.6 raw, rounded half up to 410. The up axis is +y.
constexpr fx RV_ROUTE_FOV_Y = 60 * rv_pdklib::fx_one;
constexpr fx RV_ROUTE_NEAR = 410;
constexpr fx RV_ROUTE_FAR = 100 * rv_pdklib::fx_one;
constexpr fx_vec3 RV_ROUTE_UP = { 0, rv_pdklib::fx_one, 0 };

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

} // namespace

bool rv_route_key_name(std::string_view text, int32_t &index)
{
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

int rv_route_camera_at(const std::vector<rv_route_key> &keys, int64_t frame, int64_t screen_height, rv_cg_camera &out)
{
    if (screen_height <= 0 || screen_height > std::numeric_limits<int32_t>::max()) {
        return RV_ERR_INVAL;
    }
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
    rv_pdklib::rv_gte_camera camera = {};
    const int make_code = rv_pdklib::rv_gte_camera_make(eye,
        target,
        RV_ROUTE_UP,
        RV_ROUTE_FOV_Y,
        static_cast<int32_t>(screen_height),
        RV_ROUTE_NEAR,
        RV_ROUTE_FAR,
        camera);
    if (make_code != RV_OK) {
        return make_code;
    }
    // rv_cg_camera has the same fields as rv_gte_camera; the matrix layouts match (see rv_cg.h).
    for (std::size_t i = 0; i < std::size(out.view.r); ++i) {
        out.view.r[i] = camera.view.r[i];
    }
    for (std::size_t i = 0; i < std::size(out.view.t); ++i) {
        out.view.t[i] = camera.view.t[i];
    }
    out.h = camera.h;
    out.near_plane = camera.near_plane;
    out.far_plane = camera.far_plane;
    return RV_OK;
}

namespace
{

using rv_pdkkit::ecs_id;
using rv_pdkkit::ecs_world;

// Entities with a camera component, in slot order.
std::vector<ecs_id> camera_ids(ecs_world &world)
{
    std::vector<ecs_id> ids;
    rv_pdkkit::ecs_component_each<rv_pdkkit::ecs_component_camera>(world, [&ids](ecs_id id, auto &) {
        ids.push_back(id);
    });
    return ids;
}

// The route entity and its component. RV_ERR_NOENT without one, RV_ERR_ALREADY with two: the camera would not be unique.
// Logs nothing: the frame system calls it every frame.
int find_route(ecs_world &world, ecs_id &id, ecs_component_route *&route)
{
    int count = 0;
    rv_pdkkit::ecs_component_each<ecs_component_route>(world, [&](ecs_id holder, ecs_component_route &component) {
        ++count;
        id = holder;
        route = &component;
    });
    if (count == 0) {
        return RV_ERR_NOENT;
    }
    if (count > 1) {
        return RV_ERR_ALREADY;
    }
    return RV_OK;
}

// Appends the key of camera when it is a cam_NN child of route. A cam_NN camera outside the group is warned and skipped.
int add_route_key(ecs_world &world, ecs_id camera, ecs_id route, std::vector<rv_route_key> &keys)
{
    const auto *hierarchy = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_parent>(world, camera);
    const auto *name = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_name>(world, camera);
    if (name == nullptr) {
        return RV_OK;
    }
    int32_t index = 0;
    const bool named = rv_route_key_name(std::string_view{ name->text }, index);
    if (!named) {
        return RV_OK;
    }
    const bool on_route = hierarchy != nullptr && hierarchy->parent == route;
    if (!on_route) {
        RV_LOG_WARN("scene", "camera '{}' is not in the route group: not a route key", std::string_view{ name->text });
        return RV_OK;
    }
    const auto *place = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_world>(world, camera);
    if (place == nullptr) {
        RV_LOG_ERR("scene", "scene refused: camera '{}' has no world matrix", std::string_view{ name->text });
        return RV_ERR_INVAL;
    }
    rv_route_key key = {};
    key.index = index;
    // The eye is t; the view direction is the z column of r. 4.12 and Q20.12 share the 4096 scale, so the raw values are fx.
    const auto &m = place->matrix;
    key.eye = fx_vec3{ m.t[0], m.t[1], m.t[2] };
    const fx_vec3 dir = fx_vec3{ m.r[2], m.r[5], m.r[8] };
    const int unit_code = rv_route_unit(dir, key.dir);
    if (unit_code != RV_OK) {
        RV_LOG_ERR("scene", "scene refused: camera '{}' direction leaves Q20.12", std::string_view{ name->text });
        return unit_code;
    }
    keys.push_back(key);
    return RV_OK;
}

} // namespace

const ecs_component_route *rv_route_of(ecs_world &world)
{
    const ecs_component_route *route = nullptr;
    rv_pdkkit::ecs_component_each<ecs_component_route>(world, [&route](ecs_id, ecs_component_route &component) {
        route = &component;
    });
    return route;
}

int rv_route_collect(ecs_world &world)
{
    ecs_id route_id{};
    ecs_component_route *route = nullptr;
    const int route_code = find_route(world, route_id, route);
    if (route_code != RV_OK) {
        RV_LOG_ERR("scene", "scene refused: the scene needs one route entity, rv_err {}", route_code);
        return route_code;
    }
    std::vector<rv_route_key> found;
    for (const ecs_id camera : camera_ids(world)) {
        const int key_code = add_route_key(world, camera, route_id, found);
        if (key_code != RV_OK) {
            return key_code;
        }
    }
    if (found.empty()) {
        RV_LOG_ERR("scene", "scene refused: the route has no cam_NN camera");
        return RV_ERR_NOENT;
    }
    std::sort(found.begin(), found.end(), [](const rv_route_key &a, const rv_route_key &b) {
        return a.index < b.index;
    });
    route->keys = found;
    return RV_OK;
}

int ecs_entity_route::add_defaults(ecs_world &world, ecs_id id) const
{
    ecs_component_route *route = nullptr;
    const int route_code = rv_pdkkit::ecs_component_ensure<ecs_component_route>(world, id, route);
    if (route_code != RV_OK) {
        RV_LOG_ERR("scene", "route component not added: rv_err {}", route_code);
        return route_code;
    }
    return RV_OK;
}

int ecs_system_route(ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    ecs_id route_id{};
    ecs_component_route *route = nullptr;
    const int route_code = find_route(world, route_id, route);
    if (route_code != RV_OK) {
        return route_code;
    }
    const int64_t screen_height = rv_cv_screen_height(rv_pdko_cv(frame.pdk));
    rv_cg_camera camera = {};
    const int camera_code = rv_route_camera_at(route->keys, frame.frame, screen_height, camera);
    if (camera_code != RV_OK) {
        return camera_code;
    }
    rv_pdkkit::ecs_world_camera(world, camera);
    return RV_OK;
}

} // namespace example_cpp_pdkkit
