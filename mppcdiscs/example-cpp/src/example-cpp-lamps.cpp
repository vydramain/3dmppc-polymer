#include "example-cpp-lamps.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "example-cpp-draw.hpp"

namespace example_cpp
{

namespace
{

// The light of a burning lamp: a pool on the ground and a glow at the head. Its origin is the
// lamp's base, so it takes the lamp's world matrix unchanged.
constexpr const char *RV_LAMP_LIGHT_MODEL = "lamp_light.mppcmod";
constexpr std::string_view RV_LAMP_NAME_PREFIX = "lamp_";

// A lamp burns while the eye is nearer than this, metres. Lamps stand 6 m apart on the street,
// so the reach covers the lamp the camera is passing and its neighbours.
constexpr float RV_LAMP_REACH = 8.0f;
constexpr float RV_LAMP_REACH_SQUARED = RV_LAMP_REACH * RV_LAMP_REACH;

// The route goes out along the street to the house (keys 00..09) and comes back (keys 09..17).
// Frame 540 is key 09, where the route starts to turn at the far end (cam_10 is the top of the turn);
// the turn belongs to the return half.
// Ceiling: the split holds while no lamp is within RV_LAMP_REACH of the turn (cam_09..11). A lamp
// there needs the split moved to the turn's frame.
constexpr int64_t RV_LAMP_OUTWARD_END = RV_ROUTE_PERIOD_FRAMES / 2;

// First frame in [from, to) whose eye is within the reach of at, -1 when none.
int64_t first_near(const std::vector<rv_route_key> &keys, const rv_cg_vec3 &at, int64_t from, int64_t to)
{
    for (int64_t frame = from; frame < to; ++frame) {
        const rv_cg_vec3 eye = rv_route_camera_at(keys, frame).eye;
        const float dx = eye.x - at.x;
        const float dy = eye.y - at.y;
        const float dz = eye.z - at.z;
        if (dx * dx + dy * dy + dz * dz < RV_LAMP_REACH_SQUARED) {
            return frame;
        }
    }
    return -1;
}

} // namespace

bool rv_lamp_name(const char *name)
{
    const std::string_view text = name;
    return text.size() > RV_LAMP_NAME_PREFIX.size() && text.starts_with(RV_LAMP_NAME_PREFIX);
}

void rv_lamps_schedule(const std::vector<rv_route_key> &keys, std::vector<rv_lamp> &lamps)
{
    for (rv_lamp &lamp : lamps) {
        // The translation column of the lamp's world: its base on the street.
        const rv_cg_vec3 at = { lamp.world[3], lamp.world[7], lamp.world[11] };
        const int64_t on = first_near(keys, at, 0, RV_LAMP_OUTWARD_END);
        const int64_t off = first_near(keys, at, RV_LAMP_OUTWARD_END, RV_ROUTE_PERIOD_FRAMES);
        if (on < 0 || off < 0) {
            RV_LOG_WARN("lamps", "{} is not passed by the route in both halves: it stays dark", lamp.name.c_str());
            continue;
        }
        lamp.on_frame = on;
        lamp.off_frame = off;
        RV_LOG_INFO("lamps", "{} burns from frame {} to {} of the period", lamp.name.c_str(), on, off);
    }
}

int64_t rv_lamps_draw(rv_pdko *pdk, const std::vector<rv_lamp> &lamps, int64_t frame)
{
    for (const rv_lamp &lamp : lamps) {
        if (frame < lamp.on_frame || frame >= lamp.off_frame) {
            continue;
        }
        const int64_t code = rv_draw_model(pdk, RV_LAMP_LIGHT_MODEL, lamp.world);
        if (code < 0) {
            return code;
        }
    }
    return RV_OK;
}

} // namespace example_cpp
