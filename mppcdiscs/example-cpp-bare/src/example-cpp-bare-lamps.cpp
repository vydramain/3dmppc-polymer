#include "example-cpp-bare.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "example-cpp-bare-layout.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace example_cpp_bare
{

namespace
{

// A lamp burns while the eye is nearer than this, metres. Lamps stand 8 m apart on the street (x 4, 12, 20, 28),
// so the reach covers the lamp the camera is passing; a neighbour is in reach only while the eye is between them.
// 8 m in Q20.12: 8 * 4096 raw steps.
constexpr rv_pdklib::fx RV_LAMP_REACH = 32768;

// The route goes out along the street to the house (keys 00..09) and comes back (keys 10..19).
// Frame 2160 (P/2) is key cam_10, inside the flat, where the route turns back; the turn belongs to the return half.
// Ceiling: the split holds while no lamp is within RV_LAMP_REACH of the turn keys (cam_10..cam_12). A lamp
// there needs the split moved to the turn's frame.
constexpr int64_t RV_LAMP_OUTWARD_END = RV_ROUTE_PERIOD_FRAMES / 2;

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

// Burn window of the lamp at base, named lamp_<ordinal>. A lamp that cannot be scheduled is warned once and stays
// dark: out keeps its dark value.
void schedule_lamp(const std::vector<rv_route_key> &keys, rv_pdklib::fx_vec3 base, int64_t ordinal, rv_lamp_schedule &out)
{
    int64_t on = -1;
    const int on_code = first_near(keys, base, 0, RV_LAMP_OUTWARD_END, on);
    if (on_code != RV_OK) {
        RV_LOG_WARN("lamps",
            "lamp_{}: route or lamp offset leaves Q20.12 in the outward half, it stays dark: rv_err {}",
            ordinal,
            on_code);
        return;
    }
    int64_t off = -1;
    const int off_code = first_near(keys, base, RV_LAMP_OUTWARD_END, RV_ROUTE_PERIOD_FRAMES, off);
    if (off_code != RV_OK) {
        RV_LOG_WARN("lamps",
            "lamp_{}: route or lamp offset leaves Q20.12 in the return half, it stays dark: rv_err {}",
            ordinal,
            off_code);
        return;
    }
    if (on < 0 || off < 0) {
        RV_LOG_WARN("lamps", "lamp_{} is not passed by the route in both halves: it stays dark", ordinal);
        return;
    }
    out.on_frame = on;
    out.off_frame = off;
    RV_LOG_INFO("lamps", "lamp_{} burns from frame {} to {} of the period", ordinal, on, off);
}

} // namespace

void rv_lamps_schedule(const std::vector<rv_route_key> &keys, std::vector<rv_lamp_schedule> &out)
{
    const std::span<const bare_lamp> lamps = bare_lamps();
    out.assign(lamps.size(), rv_lamp_schedule{});
    // bare_lamps() lists lamp_1..lamp_4 in order, so the 1-based position is the lamp's name in the log.
    for (std::size_t i = 0; i < lamps.size(); ++i) {
        const int64_t ordinal = static_cast<int64_t>(i) + 1;
        if (keys.empty()) {
            RV_LOG_WARN("lamps", "lamp_{}: no route keys, it stays dark", ordinal);
            continue;
        }
        schedule_lamp(keys, lamps[i].base, ordinal, out[i]);
    }
}

bool rv_lamp_lit(const rv_lamp_schedule &lamp, int64_t frame)
{
    const int64_t place = frame % RV_ROUTE_PERIOD_FRAMES;
    return lamp.on_frame <= place && place < lamp.off_frame;
}

} // namespace example_cpp_bare
