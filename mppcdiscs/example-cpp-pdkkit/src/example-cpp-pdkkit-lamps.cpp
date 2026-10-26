#include "example-cpp-pdkkit-lamps.hpp"

#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_loads.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "example-cpp-pdkkit-zones.hpp"

namespace example_cpp_pdkkit
{

namespace
{

// The log tag of the lamp messages.
constexpr const char *RV_LAMPS_TAG = "lamps";

// Holders of T, listed first: a component cannot be added or removed inside ecs_component_each.
template <typename T>
std::vector<rv_pdkkit::ecs_id> holders_of(rv_pdkkit::ecs_world &world)
{
    std::vector<rv_pdkkit::ecs_id> holders;
    rv_pdkkit::ecs_component_each<T>(world, [&holders](rv_pdkkit::ecs_id id, T &) {
        holders.push_back(id);
    });
    return holders;
}

// The entities whose parent is lamp, listed first for the same reason as holders_of.
std::vector<rv_pdkkit::ecs_id> children_of(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id lamp)
{
    std::vector<rv_pdkkit::ecs_id> children;
    rv_pdkkit::ecs_component_each<rv_pdkkit::ecs_component_parent>(world,
        [&children, lamp](rv_pdkkit::ecs_id id, rv_pdkkit::ecs_component_parent &link) {
            if (link.parent == lamp) {
                children.push_back(id);
            }
        });
    return children;
}

// Removes the lamp_switched of the last frame; the event lives one frame.
int clear_switched(rv_pdkkit::ecs_world &world)
{
    const std::vector<rv_pdkkit::ecs_id> holders = holders_of<ecs_component_lamp_switched>(world);
    for (const rv_pdkkit::ecs_id id : holders) {
        const int remove_code = rv_pdkkit::ecs_component_remove<ecs_component_lamp_switched>(world, id);
        if (remove_code != RV_OK) {
            return remove_code;
        }
    }
    return RV_OK;
}

// Warns once that the zone's lamp is not in the world, so the zone switches nothing.
void warn_missing_once(const rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id zone, ecs_component_zone_lamp &named)
{
    if (named.warned) {
        return;
    }
    named.warned = true;
    RV_LOG_WARN(RV_LAMPS_TAG,
        "zone {}: lamp {} not found; the zone switches nothing",
        rv_pdkkit::ecs_transform_label(world, zone),
        named.lamp);
}

// Sets the light of lamp to on. A change marks the lamp lamp_switched and logs it by the lamp's name. Error: the code of the
// step that fails; NOENT when lamp has no lamp_light.
int switch_lamp(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id lamp, bool on)
{
    ecs_component_lamp_light *light = rv_pdkkit::ecs_component_get<ecs_component_lamp_light>(world, lamp);
    if (light == nullptr) {
        return RV_ERR_NOENT;
    }
    if (light->on == on) {
        return RV_OK;
    }
    ecs_component_lamp_switched *switched = nullptr;
    const int ensure_code = rv_pdkkit::ecs_component_ensure<ecs_component_lamp_switched>(world, lamp, switched);
    if (ensure_code != RV_OK) {
        return ensure_code;
    }
    light->on = on;
    RV_LOG_INFO(RV_LAMPS_TAG, "{} {}", rv_pdkkit::ecs_transform_label(world, lamp), on ? "on" : "off");
    return RV_OK;
}

// Switches the lamp that zone names, by the direction of its zone_crossed along X. A zone without zone_lamp is not a lamp
// zone and is skipped. Error: the code of the step that fails.
int cross_zone(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id zone, const ecs_component_zone_crossed &crossed)
{
    // Lamps stand on one street along world X: +X is the way along it that lights a lamp, -X darkens it.
    if (crossed.direction.x == 0) {
        return RV_OK;
    }
    ecs_component_zone_lamp *named = rv_pdkkit::ecs_component_get<ecs_component_zone_lamp>(world, zone);
    if (named == nullptr) {
        return RV_OK;
    }
    rv_pdkkit::ecs_id lamp{};
    const int find_code = rv_pdkkit::ecs_find_by_name(world, std::string_view{ named->lamp }, lamp);
    if (find_code == RV_ERR_NOENT) {
        warn_missing_once(world, zone, *named);
        return RV_OK;
    }
    if (find_code != RV_OK) {
        return find_code;
    }
    return switch_lamp(world, lamp, crossed.direction.x > 0);
}

// Shows or hides one light part: hidden while dark, shown while lit. Error: the code of the step that fails; NOENT from a
// removal is not an error, the part is already shown.
int set_hidden(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id part, bool hidden)
{
    if (!hidden) {
        const int shown_code = rv_pdkkit::ecs_component_remove<rv_pdkkit::ecs_component_hidden>(world, part);
        const bool shown = shown_code == RV_OK || shown_code == RV_ERR_NOENT;
        return shown ? RV_OK : shown_code;
    }
    rv_pdkkit::ecs_component_hidden *mark = nullptr;
    const int hidden_code = rv_pdkkit::ecs_component_ensure<rv_pdkkit::ecs_component_hidden>(world, part, mark);
    if (hidden_code != RV_OK) {
        return hidden_code;
    }
    return RV_OK;
}

} // namespace

int ecs_entity_lamp::add_defaults(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id) const
{
    ecs_component_lamp_light *light = nullptr;
    const int light_code = rv_pdkkit::ecs_component_ensure<ecs_component_lamp_light>(world, id, light);
    if (light_code != RV_OK) {
        RV_LOG_ERR(RV_LAMPS_TAG, "lamp_light component not added: rv_err {}", light_code);
        return light_code;
    }
    return RV_OK;
}

int ecs_system_lamps(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &)
{
    const int clear_code = clear_switched(world);
    if (clear_code != RV_OK) {
        return clear_code;
    }
    const std::vector<rv_pdkkit::ecs_id> zones = holders_of<ecs_component_zone_crossed>(world);
    for (const rv_pdkkit::ecs_id zone : zones) {
        const ecs_component_zone_crossed *crossed = rv_pdkkit::ecs_component_get<ecs_component_zone_crossed>(world, zone);
        const int cross_code = cross_zone(world, zone, *crossed);
        if (cross_code != RV_OK) {
            return cross_code;
        }
    }
    return RV_OK;
}

int ecs_system_light(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &)
{
    const std::vector<rv_pdkkit::ecs_id> switched = holders_of<ecs_component_lamp_switched>(world);
    for (const rv_pdkkit::ecs_id lamp : switched) {
        const ecs_component_lamp_light *light = rv_pdkkit::ecs_component_get<ecs_component_lamp_light>(world, lamp);
        if (light == nullptr) {
            return RV_ERR_NOENT;
        }
        const std::vector<rv_pdkkit::ecs_id> parts = children_of(world, lamp);
        for (const rv_pdkkit::ecs_id part : parts) {
            const int code = set_hidden(world, part, !light->on);
            if (code != RV_OK) {
                return code;
            }
        }
    }
    return RV_OK;
}

} // namespace example_cpp_pdkkit
