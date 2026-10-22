#pragma once

// Entity world of the scene ECS: numbered slots with generations, the scene loads, and the registries of entity types,
// component factories and frame systems. Component storage is in rv_ecs_store.hpp; system bodies are in other headers.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

// Full header, not a forward declaration: ecs_world holds rv_cg_camera by value.
#include "pdk/cg/rv_cg.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_pdkkit
{

// Defined in rv_ecs_scene_records.hpp; only a reference is taken here, which keeps that header out of this one's includes.
struct ecs_scene_component;

// Generation 0 is never issued: a default ecs_id{} (index 0, generation 0) names no entity.
inline constexpr uint32_t ecs_first_generation = 1;
// The index is uint32_t, so the slot count stays below 2^32.
inline constexpr std::size_t ecs_slot_limit = std::numeric_limits<uint32_t>::max();
// Last generation that is issued. A slot destroyed while at it is retired, so it never wraps to 0.
inline constexpr uint32_t ecs_generation_max = std::numeric_limits<uint32_t>::max();
// Load numbers start at 1; a load is refused at ecs_load_number_limit, so the counter never wraps to 0.
inline constexpr uint32_t ecs_first_load_number = 1;
inline constexpr uint32_t ecs_load_number_limit = std::numeric_limits<uint32_t>::max();

struct ecs_id {
    uint32_t index = 0;      // slot number in detail::ecs_world_state::slots
    uint32_t generation = 0; // equals the slot's generation while the entity lives

    bool operator==(const ecs_id &) const = default;
};

struct ecs_slot {
    uint32_t generation = ecs_first_generation;
    bool live = false;
};

// Type-erased component pool, so the world can drop an entity's components without their types.
struct ecs_pool_base {
    virtual ~ecs_pool_base() = default;
    virtual void drop(ecs_id id) = 0; // does nothing when id holds no component here
    virtual const std::vector<ecs_id> &holders() const = 0;
};

struct ecs_entity;
struct ecs_world;
// Defined in rv_ecs_frame.hpp.
struct ecs_frame;

// Fills the components of entity id from the fields of a scene component. Returns RV_OK or RV_ERR_*.
// May throw std::bad_alloc before any state changes, as the pool growth does.
using ecs_component_factory = int (*)(ecs_world &, ecs_id, const ecs_scene_component &);

// Frame system: returns RV_OK or RV_ERR_*. Run by ecs_world_frame in registration order.
using ecs_system = int (*)(ecs_world &, const ecs_frame &);

// One scene load: its number and its entities in file order.
struct ecs_load {
    uint32_t number = 0;
    std::vector<ecs_id> made;
};

// What an exception left when ecs_try mapped it: thrown is set, text copies its what(). The exception object ends with
// its catch, so a view of what() would dangle.
// Fixed size of that copy: it needs no allocation in a handler, and the record stays a plain struct. A longer text is
// cut to 127 characters, so two texts equal in that prefix count as one repeat.
inline constexpr std::size_t ecs_reason_size = 128;
struct ecs_reason {
    bool thrown = false;
    char text[ecs_reason_size] = {};
};

namespace detail
{

// The world's data, kept in ecs_world::state_.
struct ecs_world_state {
    std::vector<ecs_slot> slots;
    std::vector<uint32_t> free_slots; // destroyed slots, reused by the next create
    uint32_t live_count = 0;
    std::vector<ecs_load> loads; // in load order
    uint32_t next_load_number = ecs_first_load_number;
    // The frame's draw order, rebuilt each frame; the buffer keeps its capacity.
    std::vector<ecs_id> draw_order;
    // Set while ecs_world_frame runs its systems, transform and draw.
    bool in_frame = false;
    // Numbers unloaded after the frame, in request order.
    std::vector<uint32_t> pending_unloads;
    std::vector<std::unique_ptr<ecs_pool_base>> pools; // indexed by component type number
    // Non-owning entity types; each type object must outlive the world. Component type numbers
    // are template statics, so one world belongs to one .so: another .so numbers types differently.
    std::vector<const ecs_entity *> types;
    // Component factories by name; the name views must outlive the world (string literals).
    std::vector<std::pair<std::string_view, ecs_component_factory>> factories;
    // Frame systems by name; the name views must outlive the world (string literals).
    std::vector<std::pair<std::string_view, ecs_system>> systems;
    // Camera of the frame, set through ecs_world_camera; ecs_system_draw needs camera_set.
    rv_cg_camera camera{};
    bool camera_set = false;
    // Last failure ecs_frame_run logged: system, code and reason. A frame repeating all three stays silent; a frame whose
    // systems all succeed clears them, so the disc's log shows each new failure once and not once per frame.
    std::string_view logged_system{};
    int logged_code = RV_OK;
    ecs_reason logged_reason{};
};

// The way kit code reaches the world's data. Callers bind its result with auto, so the type behind it can change.
[[nodiscard]] ecs_world_state &ecs_state(ecs_world &world) noexcept;
[[nodiscard]] const ecs_world_state &ecs_state(const ecs_world &world) noexcept;

} // namespace detail

// The data is private: kit code reaches it through detail::ecs_state, a disc through the ecs_* functions.
struct ecs_world {
private:
    detail::ecs_world_state state_;

    friend detail::ecs_world_state &detail::ecs_state(ecs_world &world) noexcept;
    friend const detail::ecs_world_state &detail::ecs_state(const ecs_world &world) noexcept;
};

namespace detail
{

[[nodiscard]] inline ecs_world_state &ecs_state(ecs_world &world) noexcept
{
    return world.state_;
}

[[nodiscard]] inline const ecs_world_state &ecs_state(const ecs_world &world) noexcept
{
    return world.state_;
}

} // namespace detail

// Index for the next entity: the newest free slot, else a new slot appended.
// Precondition: the caller has checked that an index is left (ecs_entity_create does).
// Throws only before any state changes (vector growth).
inline uint32_t ecs_take_slot(ecs_world &world)
{
    auto &state = detail::ecs_state(world);
    if (state.free_slots.empty()) {
        state.slots.push_back(ecs_slot{});
        return static_cast<uint32_t>(state.slots.size() - 1);
    }
    const uint32_t index = state.free_slots.back();
    state.free_slots.pop_back();
    return index;
}

// Frees a destroyed slot for reuse. A slot at ecs_generation_max stays retired.
// The free-list push may throw, so it runs before the generation changes.
inline void ecs_release_slot(ecs_world &world, uint32_t index)
{
    auto &state = detail::ecs_state(world);
    ecs_slot &slot = state.slots[index];
    const bool reusable = slot.generation != ecs_generation_max;
    if (reusable) {
        state.free_slots.push_back(index);
        ++slot.generation;
    }
}

// Tag of the log lines written by ecs_guard.
inline constexpr const char *ecs_core_tag = "ecs-core";

// Logs an exception that escaped an ecs operation. The log may throw too, so the caller returns its code regardless.
inline void ecs_guard_log(const char *what)
{
    try {
        RV_LOG_ERR(ecs_core_tag, "an ecs operation threw, RV_ERR_INVAL: {}", what);
    } catch (...) {
        // the line is lost
    }
}

// Copies an exception's text into reason; the copy takes no allocation, so it is safe in a handler.
inline void ecs_reason_set(ecs_reason &reason, const char *text)
{
    std::strncpy(reason.text, text, ecs_reason_size - 1);
    reason.text[ecs_reason_size - 1] = '\0';
    reason.thrown = true;
}

// Runs op and maps its exception: bad_alloc is RV_ERR_NOMEM, another exception RV_ERR_INVAL. The exception's full text
// goes to report inside the handler, where it is still whole; reason keeps a copy. A returned code passes through with
// reason.thrown clear. bad_alloc is not reported: no allocation is safe to make at that point. report must not throw:
// it runs inside the handler.
template <typename Op, typename Report>
[[nodiscard]] int ecs_try(Op &&op, ecs_reason &reason, Report &&report)
{
    reason = {};
    try {
        return op();
    } catch (const std::bad_alloc &) {
        ecs_reason_set(reason, "out of memory");
        return RV_ERR_NOMEM;
    } catch (const std::exception &e) {
        ecs_reason_set(reason, e.what());
        report(e.what());
        return RV_ERR_INVAL;
    } catch (...) {
        ecs_reason_set(reason, "unknown exception");
        report("unknown exception");
        return RV_ERR_INVAL;
    }
}

// Public ecs_* operations return int, so no exception may leave them: bad_alloc is RV_ERR_NOMEM, others RV_ERR_INVAL.
// The helpers throw only before they change state, so the world is unchanged. Only RV_ERR_INVAL from an exception is logged.
template <typename Op>
[[nodiscard]] int ecs_guard(Op &&op)
{
    ecs_reason reason;
    return ecs_try(std::forward<Op>(op), reason, ecs_guard_log);
}

// RV_ERR_NOMEM when every index is taken.
[[nodiscard]] inline int ecs_entity_create(ecs_world &world, ecs_id &out)
{
    auto &state = detail::ecs_state(world);
    return ecs_guard([&] {
        if (state.free_slots.empty() && state.slots.size() >= ecs_slot_limit) {
            return RV_ERR_NOMEM;
        }
        const uint32_t index = ecs_take_slot(world);
        ecs_slot &slot = state.slots[index];
        slot.live = true;
        ++state.live_count;
        out = ecs_id{ index, slot.generation };
        return RV_OK;
    });
}

inline bool ecs_entity_alive(const ecs_world &world, ecs_id id)
{
    const auto &state = detail::ecs_state(world);
    if (id.index >= state.slots.size()) {
        return false;
    }
    const ecs_slot &slot = state.slots[id.index];
    return slot.live && slot.generation == id.generation;
}

// RV_ERR_NOENT for an unknown or stale id. Drops the entity's components from every pool. The only throw is the
// free-list push in ecs_release_slot, before any change: pool->drop does not throw (nothrow move, rv_ecs_store.hpp).
[[nodiscard]] inline int ecs_entity_destroy(ecs_world &world, ecs_id id)
{
    auto &state = detail::ecs_state(world);
    return ecs_guard([&] {
        if (!ecs_entity_alive(world, id)) {
            return RV_ERR_NOENT;
        }
        ecs_release_slot(world, id.index);
        for (const std::unique_ptr<ecs_pool_base> &pool : state.pools) {
            if (pool) {
                pool->drop(id);
            }
        }
        ecs_slot &slot = state.slots[id.index];
        slot.live = false;
        --state.live_count;
        return RV_OK;
    });
}

inline uint32_t ecs_world_count(const ecs_world &world)
{
    return detail::ecs_state(world).live_count;
}

} // namespace rv_pdkkit
