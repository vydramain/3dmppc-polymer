#include <cstdint>
#include <vector>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "example-cpp-pdkkit-route.hpp"
#include "example-cpp-pdkkit-zones.hpp"

namespace example_cpp_pdkkit
{

namespace
{

// The log tag of the zone warnings.
constexpr const char *RV_ZONES_TAG = "zones";

// A zone of this frame: its entity, the world translation of the entity (the box centre) and the box size, Q20.12.
struct zone_box {
    rv_pdkkit::ecs_id id{};
    rv_pdklib::fx_vec3 centre{};
    rv_pdklib::fx_vec3 size{};
};

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

// World translation of id, Q20.12, from the transform as it is now: the world matrices are not computed yet this frame.
// A refusal is logged by ecs_transform_world.
int world_point(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, rv_pdklib::fx_vec3 &out)
{
    rv_pdklib::rv_gte_matrix matrix{};
    const int world_code = rv_pdkkit::ecs_transform_world(world, id, matrix);
    if (world_code != RV_OK) {
        return world_code;
    }
    out = rv_pdklib::fx_vec3{ matrix.t[0], matrix.t[1], matrix.t[2] };
    return RV_OK;
}

// One axis of the box: p in [c - s/2, c + s/2), min included, max excluded. Doubled, so s/2 is never rounded.
bool axis_inside(rv_pdklib::fx p, rv_pdklib::fx c, rv_pdklib::fx s)
{
    const int64_t twice = 2 * (static_cast<int64_t>(p) - c);
    return -static_cast<int64_t>(s) <= twice && twice < s;
}

bool inside(const rv_pdklib::fx_vec3 &point, const zone_box &zone)
{
    return axis_inside(point.x, zone.centre.x, zone.size.x) && axis_inside(point.y, zone.centre.y, zone.size.y) &&
        axis_inside(point.z, zone.centre.z, zone.size.z);
}

// Warns a zone whose size is not positive on some axis, once per zone: the flag is the zone component's working state.
void warn_size_once(const rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, ecs_component_zone &component)
{
    if (component.warned) {
        return;
    }
    component.warned = true;
    RV_LOG_WARN(RV_ZONES_TAG,
        "zone {}: size {} {} {} Q20.12 is not positive on an axis; the zone never fires",
        rv_pdkkit::ecs_transform_label(world, id),
        component.size.x,
        component.size.y,
        component.size.z);
}

// Every zone entity as a box: the centre is its world translation, the size its zone component's. A zone with a size that
// is not positive is skipped, so it never fires; the others still work.
int collect_zones(rv_pdkkit::ecs_world &world, std::vector<zone_box> &zones)
{
    const std::vector<rv_pdkkit::ecs_id> holders = holders_of<ecs_component_zone>(world);
    for (const rv_pdkkit::ecs_id id : holders) {
        ecs_component_zone *component = rv_pdkkit::ecs_component_get<ecs_component_zone>(world, id);
        const bool positive = component->size.x > 0 && component->size.y > 0 && component->size.z > 0;
        if (!positive) {
            warn_size_once(world, id, *component);
            continue;
        }
        zone_box zone{};
        zone.id = id;
        zone.size = component->size;
        const int centre_code = world_point(world, id, zone.centre);
        if (centre_code != RV_OK) {
            return centre_code;
        }
        zones.push_back(zone);
    }
    return RV_OK;
}

// Removes the zone_crossed of the last frame; the event lives one frame.
int clear_crossed(rv_pdkkit::ecs_world &world)
{
    const std::vector<rv_pdkkit::ecs_id> holders = holders_of<ecs_component_zone_crossed>(world);
    for (const rv_pdkkit::ecs_id id : holders) {
        const int remove_code = rv_pdkkit::ecs_component_remove<ecs_component_zone_crossed>(world, id);
        if (remove_code != RV_OK) {
            return remove_code;
        }
    }
    return RV_OK;
}

// Sets the zone's zone_crossed. A zone that already has one this frame keeps its first event; the second is warned.
int enter_zone(rv_pdkkit::ecs_world &world, const zone_box &zone, rv_pdkkit::ecs_id who, const rv_pdklib::fx_vec3 &direction)
{
    const ecs_component_zone_crossed *first = rv_pdkkit::ecs_component_get<ecs_component_zone_crossed>(world, zone.id);
    if (first != nullptr) {
        RV_LOG_WARN(RV_ZONES_TAG,
            "zone {}: {} entered after {} in this frame; the first event is kept",
            rv_pdkkit::ecs_transform_label(world, zone.id),
            rv_pdkkit::ecs_transform_label(world, who),
            rv_pdkkit::ecs_transform_label(world, first->who));
        return RV_OK;
    }
    ecs_component_zone_crossed *crossed = nullptr;
    const int ensure_code = rv_pdkkit::ecs_component_ensure<ecs_component_zone_crossed>(world, zone.id, crossed);
    if (ensure_code != RV_OK) {
        return ensure_code;
    }
    crossed->who = who;
    crossed->direction = direction;
    return RV_OK;
}

// Enters each zone the rider now stands in and did not stand in before. Without moved the previous position counts as
// outside every zone, and the direction is zero: the before point is then the now point.
int cross_zones(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id who, const std::vector<zone_box> &zones)
{
    rv_pdklib::fx_vec3 now{};
    const int now_code = world_point(world, who, now);
    if (now_code != RV_OK) {
        return now_code;
    }
    const ecs_component_trail *trail = rv_pdkkit::ecs_component_get<ecs_component_trail>(world, who);
    const bool moved = trail->moved;
    const rv_pdklib::fx_vec3 prev = trail->prev;
    const rv_pdklib::fx_vec3 before = moved ? prev : now;
    rv_pdklib::fx_vec3 direction{};
    const int store_code = rv_pdklib::fx_vec3_store(static_cast<int64_t>(now.x) - before.x,
        static_cast<int64_t>(now.y) - before.y,
        static_cast<int64_t>(now.z) - before.z,
        direction);
    if (store_code != RV_OK) {
        return store_code;
    }
    for (const zone_box &zone : zones) {
        const bool entered = inside(now, zone) && !(moved && inside(prev, zone));
        if (!entered) {
            continue;
        }
        const int enter_code = enter_zone(world, zone, who, direction);
        if (enter_code != RV_OK) {
            return enter_code;
        }
    }
    return RV_OK;
}

} // namespace

int ecs_system_zones(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &)
{
    const int clear_code = clear_crossed(world);
    if (clear_code != RV_OK) {
        return clear_code;
    }
    std::vector<zone_box> zones;
    const int zones_code = collect_zones(world, zones);
    if (zones_code != RV_OK) {
        return zones_code;
    }
    const std::vector<rv_pdkkit::ecs_id> riders = holders_of<ecs_component_trail>(world);
    for (const rv_pdkkit::ecs_id rider : riders) {
        const int cross_code = cross_zones(world, rider, zones);
        if (cross_code != RV_OK) {
            return cross_code;
        }
    }
    return RV_OK;
}

} // namespace example_cpp_pdkkit
