#pragma once

// Scene loads in the world: load a scene under a parent, unload it with its descendants. An unload asked for during a
// frame waits for the frame's end, so this frame still draws its entities.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_scene.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_pdkkit
{

inline constexpr const char *ecs_loads_tag = "ecs-loads";

// Position of the load in world.loads, or loads.size() when there is none.
inline std::size_t ecs_load_position(const ecs_world &world, uint32_t number)
{
    for (std::size_t i = 0; i < world.loads.size(); ++i) {
        if (world.loads[i].number == number) {
            return i;
        }
    }
    return world.loads.size();
}

// Drops dead ids from every load. A load's record stays, even empty, until its own unload removes it.
inline void ecs_loads_prune(ecs_world &world)
{
    for (ecs_load &load : world.loads) {
        std::erase_if(load.made, [&world](ecs_id id) {
            return !ecs_entity_alive(world, id);
        });
    }
}

// Whether the parent chain of id reaches one of roots. RV_ERR_INVAL for a cycle, which is logged.
// A dead or transform-less parent ends the chain without reaching anything.
inline int ecs_chain_reaches(ecs_world &world, ecs_id id, const std::vector<ecs_id> &roots, bool &reached)
{
    const ecs_component_transform *self = ecs_component_get<ecs_component_transform>(world, id);
    reached = false;
    ecs_id parent = self == nullptr ? ecs_id{} : self->parent;
    // An acyclic chain is shorter than live_count; reaching live_count steps means a cycle.
    for (uint32_t steps = 0; !reached && parent != ecs_id{}; ++steps) {
        const bool cycle = steps >= world.live_count;
        if (cycle) {
            RV_LOG_ERR(ecs_loads_tag, "entity {}:{}: parent chain is a cycle", id.index, id.generation);
            return RV_ERR_INVAL;
        }
        reached = std::find(roots.begin(), roots.end(), parent) != roots.end();
        const ecs_component_transform *up = ecs_component_get<ecs_component_transform>(world, parent);
        if (up == nullptr) {
            break;
        }
        parent = up->parent;
    }
    return RV_OK;
}

// Every live entity that is one of roots or lies under one. RV_ERR_INVAL for a cycle, found before any change.
inline int ecs_subtree_collect(ecs_world &world, const std::vector<ecs_id> &roots, std::vector<ecs_id> &out)
{
    std::vector<ecs_id> holders;
    ecs_component_each<ecs_component_transform>(world, [&holders](ecs_id id, ecs_component_transform &) {
        holders.push_back(id);
    });
    for (const ecs_id id : holders) {
        const bool is_root = std::find(roots.begin(), roots.end(), id) != roots.end();
        bool under_root = false;
        const int walked = is_root ? RV_OK : ecs_chain_reaches(world, id, roots, under_root);
        if (walked != RV_OK) {
            return walked;
        }
        if (is_root || under_root) {
            out.push_back(id);
        }
    }
    return RV_OK;
}

// Unloads now: destroys the load's entities and their descendants, then removes the load's record. Records of other
// loads, even when they are left empty, stay. RV_ERR_NOENT for an unknown number; RV_ERR_INVAL for a parent cycle,
// with nothing destroyed.
inline int ecs_scene_unload_now(ecs_world &world, uint32_t number)
{
    const std::size_t position = ecs_load_position(world, number);
    if (position == world.loads.size()) {
        return RV_ERR_NOENT;
    }
    std::vector<ecs_id> doomed;
    const int collected = ecs_subtree_collect(world, world.loads[position].made, doomed);
    if (collected != RV_OK) {
        return collected;
    }
    // Room for each destroy's free-list push, so none allocates. The reserve may throw, before any change.
    world.free_slots.reserve(world.free_slots.size() + doomed.size());
    // Each id is alive and the room is reserved, so every destroy succeeds.
    for (const ecs_id id : doomed) {
        (void)ecs_entity_destroy(world, id);
    }
    ecs_loads_prune(world);
    // Destroying changes no record, so position still names this load.
    world.loads.erase(world.loads.begin() + static_cast<std::ptrdiff_t>(position));
    return RV_OK;
}

// Best effort: the returned status is the result. Logging may need the memory that just ran out, and then the line is lost.
inline void ecs_loads_log_refused(const char *scene, const char *why)
{
    try {
        RV_LOG_ERR(ecs_loads_tag, "scene '{}': not loaded, {}", rv_pdklib::rv_log_escape(scene), why);
    } catch (const std::bad_alloc &) {
        // the line is lost; the caller still returns its code
    }
}

// Loads scene of cd under parent (ecs_id{} is the world root) and records its entities as load number.
// On error the world and the table are as before. The code is ecs_scene_read's, RV_ERR_NOMEM when no load number
// is left or the table cannot grow, or RV_ERR_INVAL for another exception.
[[nodiscard]] inline int ecs_scene_load(ecs_world &world, rv_cd *cd, const char *scene, ecs_id parent, uint32_t &number)
{
    return ecs_guard([&]() -> int {
        const bool exhausted = world.next_load_number == ecs_load_number_limit;
        if (exhausted) {
            ecs_loads_log_refused(scene, "no load number is left");
            return RV_ERR_NOMEM;
        }
        // Reserved before the entities exist, so the push below cannot throw after them.
        try {
            world.loads.reserve(world.loads.size() + 1);
        } catch (const std::bad_alloc &) {
            ecs_loads_log_refused(scene, "the load table cannot grow");
            return RV_ERR_NOMEM;
        }
        ecs_load load{ world.next_load_number, {} };
        const int read = ecs_scene_read(world, cd, scene, parent, load.made);
        if (read != RV_OK) {
            return read;
        }
        world.loads.push_back(std::move(load));
        number = world.next_load_number++;
        return RV_OK;
    });
}

// Unloads load number with its descendants. In a frame the unload is queued and returns RV_OK at once; if it later
// fails, ecs_world_frame returns that code unless the frame itself failed first. RV_ERR_NOENT for an unknown number;
// RV_ERR_NOMEM or RV_ERR_INVAL when it cannot run (ecs_guard), with nothing changed.
[[nodiscard]] inline int ecs_scene_unload(ecs_world &world, uint32_t number)
{
    return ecs_guard([&]() -> int {
        const bool unknown = ecs_load_position(world, number) == world.loads.size();
        if (unknown) {
            return RV_ERR_NOENT;
        }
        if (world.in_frame) {
            world.pending_unloads.push_back(number);
            return RV_OK;
        }
        return ecs_scene_unload_now(world, number);
    });
}

// Runs the unloads queued during a frame, in request order, and clears the queue. Each request is its own unload: a
// failed one leaves its load as it was and is not retried, and the rest still run. A repeated request for a number
// already unloaded is skipped, not an error. Returns the first failure's code.
[[nodiscard]] inline int ecs_unload_pending(ecs_world &world)
{
    return ecs_guard([&] {
        int first_error = RV_OK;
        for (const uint32_t number : world.pending_unloads) {
            const int code = ecs_guard([&] {
                return ecs_scene_unload_now(world, number);
            });
            const bool failed = code != RV_OK && code != RV_ERR_NOENT;
            const bool first_failure = failed && first_error == RV_OK;
            first_error = first_failure ? code : first_error;
        }
        world.pending_unloads.clear();
        return first_error;
    });
}

} // namespace rv_pdkkit
