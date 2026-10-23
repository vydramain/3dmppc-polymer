#include "example-cpp-pdkkit-lamps.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "example-cpp-pdkkit-route.hpp"

namespace example_cpp_pdkkit
{

namespace
{

// Reach of a lamp, metres: the eye nearer than this is in the lamp's reach, and each entry into it toggles the lamp's
// light. Lamps stand 8 m apart on the street (x 4, 12, 20, 28), so the reach covers the lamp the camera is passing;
// a neighbour is in reach only while the eye is between them.
// 8 m in Q20.12: 8 * 4096 raw steps.
constexpr rv_pdklib::fx RV_LAMP_REACH = 32768;

// The light's model on the disc, drawn at the lamp's place.
constexpr std::string_view RV_LAMP_LIGHT_MODEL = "lamp_light.gltf";

// Base of a lamp in Q20.12: the translation of its world matrix.
rv_pdklib::fx_vec3 lamp_base(const rv_pdkkit::ecs_component_world &place)
{
    const auto &t = place.matrix.t;
    return rv_pdklib::fx_vec3{ t[0], t[1], t[2] };
}

// near is true when the eye of frame phase is within RV_LAMP_REACH of lamp id.
// Error: no route keys or no world matrix (RV_ERR_NOENT), or the Q20.12 math leaves its range; near is untouched.
int lamp_near(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, const ecs_component_route *route, int64_t phase, bool &near)
{
    const bool no_keys = route == nullptr || route->keys.empty();
    if (no_keys) {
        return RV_ERR_NOENT;
    }
    const auto *place = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_world>(world, id);
    if (place == nullptr) {
        return RV_ERR_NOENT;
    }
    rv_pdklib::fx_vec3 eye = {};
    const int eye_code = rv_route_eye_at(route->keys, phase, eye);
    if (eye_code != RV_OK) {
        return eye_code;
    }
    rv_pdklib::fx_vec3 offset = {};
    const int sub_code = rv_pdklib::fx_vec3_sub(eye, lamp_base(*place), offset);
    if (sub_code != RV_OK) {
        return sub_code;
    }
    rv_pdklib::fx length = 0;
    const int length_code = rv_pdklib::fx_vec3_length(offset, length);
    if (length_code != RV_OK) {
        return length_code;
    }
    near = length < RV_LAMP_REACH;
    return RV_OK;
}

// Adds the components of the light entity id, a child of lamp: identity transform, the model, hidden (dark).
// Error: the code of the component that fails; the caller destroys id.
int add_light_components(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, rv_pdkkit::ecs_id lamp)
{
    rv_pdkkit::ecs_component_parent *link = nullptr;
    const int parent_code = rv_pdkkit::ecs_component_ensure<rv_pdkkit::ecs_component_parent>(world, id, link);
    if (parent_code != RV_OK) {
        return parent_code;
    }
    link->parent = lamp;
    rv_pdkkit::ecs_component_transform *transform = nullptr;
    const int transform_code = rv_pdkkit::ecs_component_ensure<rv_pdkkit::ecs_component_transform>(world, id, transform);
    if (transform_code != RV_OK) {
        return transform_code;
    }
    rv_pdkkit::ecs_component_model *model = nullptr;
    const int model_code = rv_pdkkit::ecs_component_ensure<rv_pdkkit::ecs_component_model>(world, id, model);
    if (model_code != RV_OK) {
        return model_code;
    }
    std::copy(RV_LAMP_LIGHT_MODEL.begin(), RV_LAMP_LIGHT_MODEL.end(), model->source);
    std::string reason;
    const int derive_code = rv_pdkkit::ecs_component_model::ecs_derive(*model, reason);
    if (derive_code != RV_OK) {
        RV_LOG_ERR("lamps", "lamp light model refused: {}", reason);
        return derive_code;
    }
    rv_pdkkit::ecs_component_hidden *hidden = nullptr;
    const int hidden_code = rv_pdkkit::ecs_component_ensure<rv_pdkkit::ecs_component_hidden>(world, id, hidden);
    if (hidden_code != RV_OK) {
        return hidden_code;
    }
    return RV_OK;
}

// Makes the light entity of lamp unless it is alive. Error: the code of the step that fails; no entity is left behind.
int ensure_light(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id lamp, ecs_component_lamp_light &light)
{
    const bool exists = rv_pdkkit::ecs_entity_alive(world, light.light);
    if (exists) {
        return RV_OK;
    }
    rv_pdkkit::ecs_id id{};
    const int created = rv_pdkkit::ecs_entity_create(world, id);
    if (created != RV_OK) {
        return created;
    }
    const int added = add_light_components(world, id, lamp);
    if (added != RV_OK) {
        const int destroyed = rv_pdkkit::ecs_entity_destroy(world, id);
        if (destroyed != RV_OK) {
            RV_LOG_WARN("lamps", "half-made light entity not destroyed: rv_err {}", destroyed);
        }
        return added;
    }
    light.light = id;
    return RV_OK;
}

// Shows the light entity when burns, else hides it with the hidden mark.
// Error: the component's code. NOENT when a shown light is asked to show again is not an error.
int set_light(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id light, bool burns)
{
    if (burns) {
        const int shown_code = rv_pdkkit::ecs_component_remove<rv_pdkkit::ecs_component_hidden>(world, light);
        const bool shown = shown_code == RV_OK || shown_code == RV_ERR_NOENT;
        if (!shown) {
            return shown_code;
        }
        return RV_OK;
    }
    rv_pdkkit::ecs_component_hidden *hidden = nullptr;
    const int hidden_code = rv_pdkkit::ecs_component_ensure<rv_pdkkit::ecs_component_hidden>(world, light, hidden);
    if (hidden_code != RV_OK) {
        return hidden_code;
    }
    return RV_OK;
}

// Warns once that lamp cannot be placed, so its light stays dark.
void warn_unplaced(const rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id lamp, ecs_component_lamp_light &light, int code)
{
    if (light.warned) {
        return;
    }
    light.warned = true;
    RV_LOG_WARN("lamps",
        "{}: no world matrix, route keys or Q20.12 room, its light stays dark: rv_err {}",
        rv_pdkkit::ecs_transform_label(world, lamp),
        code);
}

// One lamp's frame: its light exists; an approach of the eye toggles the light, and the light shows or hides.
// Error: the code of making or showing the light. An unplaced lamp is warned and dark; it is not an error.
int update_lamp(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id lamp, const ecs_component_route *route, int64_t phase)
{
    ecs_component_lamp_light *light = rv_pdkkit::ecs_component_get<ecs_component_lamp_light>(world, lamp);
    if (light == nullptr) {
        return RV_ERR_NOENT;
    }
    const int made_code = ensure_light(world, lamp, *light);
    if (made_code != RV_OK) {
        return made_code;
    }
    bool near = false;
    const int near_code = lamp_near(world, lamp, route, phase, near);
    if (near_code != RV_OK) {
        warn_unplaced(world, lamp, *light, near_code);
        light->burns = false;
        return set_light(world, light->light, false);
    }
    const bool approach = near && !light->was_near;
    light->was_near = near;
    if (approach) {
        light->burns = !light->burns;
    }
    return set_light(world, light->light, light->burns);
}

} // namespace

int ecs_entity_lamp::add_defaults(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id) const
{
    ecs_component_lamp_light *light = nullptr;
    const int light_code = rv_pdkkit::ecs_component_ensure<ecs_component_lamp_light>(world, id, light);
    if (light_code != RV_OK) {
        RV_LOG_ERR("lamps", "lamp_light component not added: rv_err {}", light_code);
        return light_code;
    }
    return RV_OK;
}

int ecs_system_lamp_light(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    const ecs_component_route *route = rv_route_of(world);
    const int64_t phase = frame.frame % RV_ROUTE_PERIOD_FRAMES;
    // Collected first: a light is made inside the update, and a component cannot be added inside ecs_component_each.
    std::vector<rv_pdkkit::ecs_id> lamps;
    rv_pdkkit::ecs_component_each<ecs_component_lamp_light>(world, [&lamps](rv_pdkkit::ecs_id id, ecs_component_lamp_light &) {
        lamps.push_back(id);
    });
    for (const rv_pdkkit::ecs_id lamp : lamps) {
        const int code = update_lamp(world, lamp, route, phase);
        if (code != RV_OK) {
            return code;
        }
    }
    return RV_OK;
}

int ecs_draw_lamps(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    std::vector<rv_pdkkit::ecs_id> order;
    rv_pdkkit::ecs_component_each<ecs_component_lamp_light>(world,
        [&world, &order](rv_pdkkit::ecs_id, ecs_component_lamp_light &light) {
            if (rv_pdkkit::ecs_entity_alive(world, light.light)) {
                order.push_back(light.light);
            }
        });
    return rv_pdkkit::ecs_system_draw(world, frame, order);
}

} // namespace example_cpp_pdkkit
