#include "example-cpp-route.hpp"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <string_view>

namespace example_cpp
{

namespace
{

// Lens and clip planes; the far plane is a margin past the scene's extent.
constexpr float RV_ROUTE_FOV_Y = 60.0f * std::numbers::pi_v<float> / 180.0f;
constexpr float RV_ROUTE_NEAR = 0.1f;
constexpr float RV_ROUTE_FAR = 100.0f;
constexpr rv_cg_vec3 RV_ROUTE_UP = { 0.0f, 1.0f, 0.0f };

// Below this length an interpolated direction has no heading; the camera looks down +z.
constexpr float RV_ROUTE_MIN_LENGTH = 1e-6f;
constexpr rv_cg_vec3 RV_ROUTE_FALLBACK_DIR = { 0.0f, 0.0f, 1.0f };

// The cam_NN name is "cam_" and exactly two digits.
constexpr std::string_view RV_ROUTE_KEY_PREFIX = "cam_";
constexpr std::size_t RV_ROUTE_KEY_DIGITS = 2;
constexpr std::size_t RV_ROUTE_KEY_NAME_SIZE = RV_ROUTE_KEY_PREFIX.size() + RV_ROUTE_KEY_DIGITS;

// One component of the uniform Catmull-Rom segment from p1 to p2, t in [0, 1].
float spline_component(float p0, float p1, float p2, float p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float slope = -p0 + p2;
    const float curve = 2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3;
    const float cubic = -p0 + 3.0f * p1 - 3.0f * p2 + p3;
    return 0.5f * (2.0f * p1 + slope * t + curve * t2 + cubic * t3);
}

rv_cg_vec3 spline(const rv_cg_vec3 &p0, const rv_cg_vec3 &p1, const rv_cg_vec3 &p2, const rv_cg_vec3 &p3, float t)
{
    return { spline_component(p0.x, p1.x, p2.x, p3.x, t),
        spline_component(p0.y, p1.y, p2.y, p3.y, t),
        spline_component(p0.z, p1.z, p2.z, p3.z, t) };
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
    index = (tens - '0') * 10 + (ones - '0');
    return true;
}

rv_cg_vec3 rv_route_unit(const rv_cg_vec3 &v)
{
    const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length < RV_ROUTE_MIN_LENGTH) {
        return RV_ROUTE_FALLBACK_DIR;
    }
    return { v.x / length, v.y / length, v.z / length };
}

rv_cg_camera rv_route_camera_at(const std::vector<rv_route_key> &keys, int64_t frame)
{
    // Segment s runs from key s to key s+1 over an equal share of the period. The
    // integer split keeps the position exact per frame: no float sum over frames.
    const int64_t count = static_cast<int64_t>(keys.size());
    const int64_t scaled = (frame % RV_ROUTE_PERIOD_FRAMES) * count;
    const int64_t seg = scaled / RV_ROUTE_PERIOD_FRAMES;
    const float t = static_cast<float>(scaled % RV_ROUTE_PERIOD_FRAMES) / static_cast<float>(RV_ROUTE_PERIOD_FRAMES);

    // The route is closed: the keys wrap around, so the last segment ends on key 0.
    const rv_route_key &p0 = keys[(seg + count - 1) % count];
    const rv_route_key &p1 = keys[seg];
    const rv_route_key &p2 = keys[(seg + 1) % count];
    const rv_route_key &p3 = keys[(seg + 2) % count];

    const rv_cg_vec3 eye = spline(p0.eye, p1.eye, p2.eye, p3.eye, t);
    const rv_cg_vec3 dir = rv_route_unit(spline(p0.dir, p1.dir, p2.dir, p3.dir, t));

    rv_cg_camera camera = {};
    camera.eye = eye;
    camera.target = { eye.x + dir.x, eye.y + dir.y, eye.z + dir.z };
    camera.up = RV_ROUTE_UP;
    camera.fov_y = RV_ROUTE_FOV_Y;
    camera.near_plane = RV_ROUTE_NEAR;
    camera.far_plane = RV_ROUTE_FAR;
    return camera;
}

} // namespace example_cpp
