#include "example-cpp-lamps.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "example-cpp-draw.hpp"

namespace example_cpp
{

namespace
{

// The light of a burning lamp: a pool on the ground and a glow at the head. Its origin is the
// lamp's base, so it takes the lamp's world matrix unchanged.
constexpr const char *RV_LAMP_LIGHT_MODEL = "lamp_light.mppcmod";
constexpr std::string_view RV_LAMP_NAME_PREFIX = "lamp_";

// A lamp burns while the eye is nearer than this, metres. Lamps stand 8 m apart on the street (x 4, 12, 20, 28),
// so the reach covers the lamp the camera is passing; a neighbour is in reach only while the eye is between them.
// 8 m in Q20.12: 8 * 4096 raw steps.
constexpr rv_pdklib::fx RV_LAMP_REACH = 32768;

// The route goes out along the street to the house (keys 00..09) and comes back (keys 10..19).
// Frame 2160 (P/2) is key cam_10, inside the flat at (35.3, 7.4, 0), where the route turns back (cam_10..cam_12);
// the turn belongs to the return half.
// Ceiling: the split holds while no lamp is within RV_LAMP_REACH of the turn keys (cam_10..cam_12). A lamp
// there needs the split moved to the turn's frame.
constexpr int64_t RV_LAMP_OUTWARD_END = RV_ROUTE_PERIOD_FRAMES / 2;

// Base of a lamp in Q20.12: the translation column of its world. Error: a coordinate leaves fx; out is untouched.
int lamp_base(const rv_lamp &lamp, rv_pdklib::fx_vec3 &out)
{
    rv_pdklib::fx_vec3 base = {};
    const int x_code = rv_pdklib::fx_from_double(lamp.world[3], base.x);
    if (x_code != RV_OK) {
        return x_code;
    }
    const int y_code = rv_pdklib::fx_from_double(lamp.world[7], base.y);
    if (y_code != RV_OK) {
        return y_code;
    }
    const int z_code = rv_pdklib::fx_from_double(lamp.world[11], base.z);
    if (z_code != RV_OK) {
        return z_code;
    }
    out = base;
    return RV_OK;
}

// First frame in [from, to) whose eye is within the reach of at; -1 in out when none.
// Error: the route or the Q20.12 math fails; out is untouched.
int first_near(const std::vector<rv_route_key> &keys, rv_pdklib::fx_vec3 at, int64_t from, int64_t to, int64_t &out)
{
    for (int64_t frame = from; frame < to; ++frame) {
        rv_pdklib::fx_vec3 eye = {};
        const int eye_code = rv_route_eye_at(keys, frame, eye);
        if (eye_code != RV_OK) {
            return eye_code;
        }
        rv_pdklib::fx_vec3 offset = {};
        const int sub_code = rv_pdklib::fx_vec3_sub(eye, at, offset);
        if (sub_code != RV_OK) {
            return sub_code;
        }
        rv_pdklib::fx length = 0;
        const int length_code = rv_pdklib::fx_vec3_length(offset, length);
        if (length_code != RV_OK) {
            return length_code;
        }
        if (length < RV_LAMP_REACH) {
            out = frame;
            return RV_OK;
        }
    }
    out = -1;
    return RV_OK;
}

} // namespace

bool rv_lamp_name(const char *name)
{
    const std::string_view text = name;
    return text.size() > RV_LAMP_NAME_PREFIX.size() && text.starts_with(RV_LAMP_NAME_PREFIX);
}

int rv_lamps_schedule(const std::vector<rv_route_key> &keys, std::vector<rv_lamp> &lamps)
{
    for (rv_lamp &lamp : lamps) {
        rv_pdklib::fx_vec3 at = {};
        const int at_code = lamp_base(lamp, at);
        if (at_code != RV_OK) {
            RV_LOG_ERR("lamps", "{}: base leaves Q20.12, not scheduled: rv_err {}", lamp.name.c_str(), at_code);
            return at_code;
        }
        int64_t on = -1;
        const int on_code = first_near(keys, at, 0, RV_LAMP_OUTWARD_END, on);
        if (on_code != RV_OK) {
            RV_LOG_ERR("lamps",
                "{}: route or lamp offset leaves Q20.12 in the outward half: rv_err {}",
                lamp.name.c_str(),
                on_code);
            return on_code;
        }
        int64_t off = -1;
        const int off_code = first_near(keys, at, RV_LAMP_OUTWARD_END, RV_ROUTE_PERIOD_FRAMES, off);
        if (off_code != RV_OK) {
            RV_LOG_ERR("lamps",
                "{}: route or lamp offset leaves Q20.12 in the return half: rv_err {}",
                lamp.name.c_str(),
                off_code);
            return off_code;
        }
        if (on < 0 || off < 0) {
            RV_LOG_WARN("lamps", "{} is not passed by the route in both halves: it stays dark", lamp.name.c_str());
            continue;
        }
        lamp.on_frame = on;
        lamp.off_frame = off;
        RV_LOG_INFO("lamps", "{} burns from frame {} to {} of the period", lamp.name.c_str(), on, off);
    }
    return RV_OK;
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
