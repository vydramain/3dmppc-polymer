#include "example-cpp-lamps.hpp"

#include <cstdint>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_ecs/rv_ecs.hpp"
#include "pdklib/rv_ecs/rv_ecs_component.hpp"
#include "pdklib/rv_ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_ecs/rv_ecs_store.hpp"
#include "pdklib/rv_ecs/rv_ecs_transform.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "example-cpp-route.hpp"

namespace example_cpp
{

namespace
{

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

// A lamp_light entity and whether it burns at the frame.
struct lamp_state {
    rv_pdklib::ecs_id id{};
    bool burns = false;
};

// Base of a lamp in Q20.12: the translation column of its world. Error: a coordinate leaves fx; out is untouched.
int lamp_base(const rv_pdklib::ecs_component_world &place, rv_pdklib::fx_vec3 &out)
{
    rv_pdklib::fx_vec3 base = {};
    const int x_code = rv_pdklib::fx_from_double(place.matrix.m[0][3], base.x);
    if (x_code != RV_OK) {
        return x_code;
    }
    const int y_code = rv_pdklib::fx_from_double(place.matrix.m[1][3], base.y);
    if (y_code != RV_OK) {
        return y_code;
    }
    const int z_code = rv_pdklib::fx_from_double(place.matrix.m[2][3], base.z);
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

// Schedule of one lamp_light entity, its base taken from the world matrix of its parent (lamp_N).
// Error: the entity or its parent lacks a component, a Q20.12 failure or the route math; the entity keeps its schedule.
int schedule_lamp(rv_pdklib::ecs_world &world, rv_pdklib::ecs_id id, const std::vector<rv_route_key> &keys)
{
    const auto *transform = rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_transform>(world, id);
    if (transform == nullptr) {
        RV_LOG_ERR("lamps", "lamp_light entity has no transform: not scheduled");
        return RV_ERR_NOENT;
    }
    const auto *name = rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_name>(world, transform->parent);
    const auto *place = rv_pdklib::ecs_component_get<rv_pdklib::ecs_component_world>(world, transform->parent);
    if (name == nullptr || place == nullptr) {
        RV_LOG_ERR("lamps", "lamp_light entity has no parent with a name and a world matrix: not scheduled");
        return RV_ERR_NOENT;
    }
    ecs_component_lamp_light *light = rv_pdklib::ecs_component_get<ecs_component_lamp_light>(world, id);
    if (light == nullptr) {
        RV_LOG_ERR("lamps", "{}: no lamp_light component: not scheduled", name->text.c_str());
        return RV_ERR_NOENT;
    }
    rv_pdklib::fx_vec3 at = {};
    const int at_code = lamp_base(*place, at);
    if (at_code != RV_OK) {
        RV_LOG_ERR("lamps", "{}: base leaves Q20.12, not scheduled: rv_err {}", name->text.c_str(), at_code);
        return at_code;
    }
    int64_t on = -1;
    const int on_code = first_near(keys, at, 0, RV_LAMP_OUTWARD_END, on);
    if (on_code != RV_OK) {
        RV_LOG_ERR("lamps",
            "{}: route or lamp offset leaves Q20.12 in the outward half: rv_err {}",
            name->text.c_str(),
            on_code);
        return on_code;
    }
    int64_t off = -1;
    const int off_code = first_near(keys, at, RV_LAMP_OUTWARD_END, RV_ROUTE_PERIOD_FRAMES, off);
    if (off_code != RV_OK) {
        RV_LOG_ERR("lamps",
            "{}: route or lamp offset leaves Q20.12 in the return half: rv_err {}",
            name->text.c_str(),
            off_code);
        return off_code;
    }
    const bool passed = on >= 0 && off >= 0;
    if (!passed) {
        RV_LOG_WARN("lamps", "{} is not passed by the route in both halves: it stays dark", name->text.c_str());
        return RV_OK;
    }
    light->on_frame = on;
    light->off_frame = off;
    RV_LOG_INFO("lamps", "{} burns from frame {} to {} of the period", name->text.c_str(), on, off);
    return RV_OK;
}

} // namespace

int ecs_lamp_light_factory(rv_pdklib::ecs_world &world, rv_pdklib::ecs_id id, const rv_cd_scene_component &component)
{
    return rv_pdklib::ecs_marker_factory<ecs_component_lamp_light>(world, id, component);
}

int ecs_system_lamp_light(rv_pdklib::ecs_world &world, const rv_pdklib::ecs_frame &frame)
{
    const int64_t phase = frame.frame % RV_ROUTE_PERIOD_FRAMES;
    // Collected first: the hidden mark is added and removed only after the walk.
    std::vector<lamp_state> states;
    rv_pdklib::ecs_component_each<ecs_component_lamp_light>(world,
        [&states, phase](rv_pdklib::ecs_id id, ecs_component_lamp_light &light) {
            const bool burns = light.on_frame <= phase && phase < light.off_frame;
            states.push_back(lamp_state{ id, burns });
        });
    for (const lamp_state &state : states) {
        if (state.burns) {
            // NOENT: the lamp has no hidden mark, so it is shown already.
            rv_pdklib::ecs_component_remove<rv_pdklib::ecs_component_hidden>(world, state.id);
        } else {
            const rv_pdklib::ecs_component_hidden *hidden =
                rv_pdklib::ecs_component_ensure<rv_pdklib::ecs_component_hidden>(world, state.id);
            if (hidden == nullptr) {
                return RV_ERR_NOENT;
            }
        }
    }
    return RV_OK;
}

int rv_lamps_schedule(rv_pdklib::ecs_world &world, const std::vector<rv_route_key> &keys)
{
    std::vector<rv_pdklib::ecs_id> lights;
    rv_pdklib::ecs_component_each<ecs_component_lamp_light>(world, [&lights](rv_pdklib::ecs_id id, ecs_component_lamp_light &) {
        lights.push_back(id);
    });
    for (const rv_pdklib::ecs_id id : lights) {
        const int code = schedule_lamp(world, id, keys);
        if (code != RV_OK) {
            return code;
        }
    }
    return RV_OK;
}

} // namespace example_cpp
