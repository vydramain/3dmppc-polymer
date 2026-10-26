#pragma once

// Scene loads in the world: load a scene under a parent, unload it with its descendants. An unload asked for during a
// frame waits for the frame's end, so this frame still draws its entities.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string_view>
#include <type_traits>
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

// Position of the load in the world's loads, or loads.size() when there is none.
inline std::size_t ecs_load_position(const ecs_world &world, uint32_t number)
{
    const auto &state = detail::ecs_state(world);
    for (std::size_t i = 0; i < state.loads.size(); ++i) {
        if (state.loads[i].number == number) {
            return i;
        }
    }
    return state.loads.size();
}

// Drops dead ids from every load. A record stays, even empty, except those ecs_scene_unload_now removes.
inline void ecs_loads_prune(ecs_world &world)
{
    auto &state = detail::ecs_state(world);
    for (ecs_load &load : state.loads) {
        std::erase_if(load.made, [&world](ecs_id id) {
            return !ecs_entity_alive(world, id);
        });
    }
}

// Whether the parent chain of id reaches one of roots. RV_ERR_INVAL for a cycle, which is logged.
// The chain follows ecs_component_parent. The parent is matched against roots before its own link is read, so a root
// is reached even when it is dead: a child of a dead load root is still in the load's subtree. A dead parent ends the
// chain; an alive one without a transform is passed through.
inline int ecs_chain_reaches(ecs_world &world, ecs_id id, const std::vector<ecs_id> &roots, bool &reached)
{
    const auto &state = detail::ecs_state(world);
    const ecs_component_parent *link = ecs_component_get<ecs_component_parent>(world, id);
    reached = false;
    ecs_id parent = link == nullptr ? ecs_id{} : link->parent;
    // An acyclic chain is shorter than live_count; reaching live_count steps means a cycle.
    for (uint32_t steps = 0; !reached && parent != ecs_id{}; ++steps) {
        const bool cycle = steps >= state.live_count;
        if (cycle) {
            RV_LOG_ERR(ecs_loads_tag, "entity {}:{}: parent chain is a cycle", id.index, id.generation);
            return RV_ERR_INVAL;
        }
        reached = std::find(roots.begin(), roots.end(), parent) != roots.end();
        const ecs_component_parent *up = ecs_component_get<ecs_component_parent>(world, parent);
        parent = up == nullptr ? ecs_id{} : up->parent;
    }
    return RV_OK;
}

// Every live root, with or without a transform, then every other live entity with a parent link under a root, each
// once. Roots come first, the rest in ecs_component_parent pool order. RV_ERR_INVAL for a cycle, found before any change.
inline int ecs_subtree_collect(ecs_world &world, const std::vector<ecs_id> &roots, std::vector<ecs_id> &out)
{
    // ponytail: linear scans over roots; a sorted id list or a set if a load grows to thousands of objects.
    for (const ecs_id id : roots) {
        const bool listed = std::find(out.begin(), out.end(), id) != out.end();
        if (ecs_entity_alive(world, id) && !listed) {
            out.push_back(id);
        }
    }
    std::vector<ecs_id> holders;
    ecs_component_each<ecs_component_parent>(world, [&holders](ecs_id id, ecs_component_parent &) {
        holders.push_back(id);
    });
    for (const ecs_id id : holders) {
        const bool is_root = std::find(roots.begin(), roots.end(), id) != roots.end();
        bool under_root = false;
        const int walked = is_root ? RV_OK : ecs_chain_reaches(world, id, roots, under_root);
        if (walked != RV_OK) {
            return walked;
        }
        if (under_root) {
            out.push_back(id);
        }
    }
    return RV_OK;
}

// Whether load has live entities and every one of them is in doomed, so this unload destroys all of them.
// ponytail: up to made x doomed comparisons per other load, inside one unload; a set of doomed ids once that product gets
// large.
inline bool ecs_load_emptied(const ecs_world &world, const ecs_load &load, const std::vector<ecs_id> &doomed)
{
    bool any_live = false;
    for (const ecs_id id : load.made) {
        const bool dead = !ecs_entity_alive(world, id);
        if (dead) {
            continue;
        }
        any_live = true;
        const bool outside = std::find(doomed.begin(), doomed.end(), id) == doomed.end();
        if (outside) {
            return false;
        }
    }
    return any_live;
}

// Unloads now: destroys the load's entities and their descendants, then removes the load's record and the records of
// the other loads this unload emptied. A record that was empty before stays until its own unload. RV_ERR_NOENT for an
// unknown number; RV_ERR_INVAL for a parent cycle, with nothing destroyed.
inline int ecs_scene_unload_now(ecs_world &world, uint32_t number)
{
    auto &state = detail::ecs_state(world);
    const std::size_t position = ecs_load_position(world, number);
    if (position == state.loads.size()) {
        return RV_ERR_NOENT;
    }
    std::vector<ecs_id> doomed;
    const int collected = ecs_subtree_collect(world, state.loads[position].made, doomed);
    if (collected != RV_OK) {
        return collected;
    }
    // Records to remove, this load's first. Built before any destroy, so a failure here changes nothing.
    std::vector<uint32_t> gone{ number };
    for (const ecs_load &load : state.loads) {
        const bool other = load.number != number;
        if (other && ecs_load_emptied(world, load, doomed)) {
            gone.push_back(load.number);
        }
    }
    // Room for each destroy's free-list push, so none allocates. The reserve may throw, before any change.
    state.free_slots.reserve(state.free_slots.size() + doomed.size());
    // Each id is alive and the room is reserved, so every destroy succeeds.
    for (const ecs_id id : doomed) {
        (void)ecs_entity_destroy(world, id);
    }
    ecs_loads_prune(world);
    // Erasing a record does not allocate, and destroying changed no record number.
    static_assert(std::is_nothrow_move_assignable_v<ecs_load>, "the erase after the destroys must not throw");
    std::erase_if(state.loads, [&gone](const ecs_load &load) {
        return std::find(gone.begin(), gone.end(), load.number) != gone.end();
    });
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
    auto &state = detail::ecs_state(world);
    return ecs_guard([&]() -> int {
        const bool exhausted = state.next_load_number == ecs_load_number_limit;
        if (exhausted) {
            ecs_loads_log_refused(scene, "no load number is left");
            return RV_ERR_NOMEM;
        }
        // Reserved before the entities exist, so the push below cannot throw after them.
        try {
            state.loads.reserve(state.loads.size() + 1);
        } catch (const std::bad_alloc &) {
            ecs_loads_log_refused(scene, "the load table cannot grow");
            return RV_ERR_NOMEM;
        }
        ecs_load load{ state.next_load_number, {} };
        const int read = ecs_scene_read(world, cd, scene, parent, load.made);
        if (read != RV_OK) {
            return read;
        }
        state.loads.push_back(std::move(load));
        number = state.next_load_number++;
        return RV_OK;
    });
}

// Unloads load number with its descendants and the records of loads it empties (ecs_scene_unload_now). In a frame the
// unload is queued and returns RV_OK at once; if it later fails, ecs_world_frame returns that code unless the frame
// itself failed first. RV_ERR_NOENT for an unknown number; RV_ERR_NOMEM or RV_ERR_INVAL when it cannot run (ecs_guard),
// with nothing changed.
[[nodiscard]] inline int ecs_scene_unload(ecs_world &world, uint32_t number)
{
    auto &state = detail::ecs_state(world);
    return ecs_guard([&]() -> int {
        const bool unknown = ecs_load_position(world, number) == state.loads.size();
        if (unknown) {
            return RV_ERR_NOENT;
        }
        if (state.in_frame) {
            state.pending_unloads.push_back(number);
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
    auto &state = detail::ecs_state(world);
    return ecs_guard([&] {
        int first_error = RV_OK;
        for (const uint32_t number : state.pending_unloads) {
            const int code = ecs_guard([&] {
                return ecs_scene_unload_now(world, number);
            });
            const bool failed = code != RV_OK && code != RV_ERR_NOENT;
            const bool first_failure = failed && first_error == RV_OK;
            first_error = first_failure ? code : first_error;
        }
        state.pending_unloads.clear();
        return first_error;
    });
}

// Calls fn(id) for each live entity of load number, in file order: the ids the load created, its made list. fn returns
// RV_OK to go on; any other code stops the walk and is the result, so fn's own RV_ERR_NOENT is returned as is. An
// unknown number returns RV_ERR_NOENT before fn runs. fn must not change the world; the walk iterates the load's list
// in place. An exception from fn maps as in ecs_guard: bad_alloc is RV_ERR_NOMEM, another exception RV_ERR_INVAL, logged.
template <typename Fn>
[[nodiscard]] int ecs_scene_entities(const ecs_world &world, uint32_t number, Fn &&fn)
{
    const auto &state = detail::ecs_state(world);
    return ecs_guard([&]() -> int {
        const std::size_t position = ecs_load_position(world, number);
        const bool unknown = position == state.loads.size();
        if (unknown) {
            return RV_ERR_NOENT;
        }
        for (const ecs_id id : state.loads[position].made) {
            const bool dead = !ecs_entity_alive(world, id);
            if (dead) {
                continue;
            }
            const int code = fn(id);
            if (code != RV_OK) {
                return code;
            }
        }
        return RV_OK;
    });
}

// First live entity named name, in slot index order, loaded or not. RV_ERR_NOENT when none matches; out is written only
// on RV_OK.
[[nodiscard]] inline int ecs_find_by_name(const ecs_world &world, std::string_view name, ecs_id &out)
{
    return ecs_guard([&]() -> int {
        // RV_ERR_ALREADY stops the walk at the match; find maps it to RV_OK.
        const int walked = ecs_entities_each(world, [&](ecs_id id) -> int {
            const ecs_component_name *named = ecs_component_get<ecs_component_name>(world, id);
            const bool match = named != nullptr && std::string_view{ named->text } == name;
            if (!match) {
                return RV_OK;
            }
            out = id;
            return RV_ERR_ALREADY;
        });
        if (walked == RV_ERR_ALREADY) {
            return RV_OK;
        }
        if (walked == RV_OK) {
            return RV_ERR_NOENT;
        }
        return walked;
    });
}

} // namespace rv_pdkkit
