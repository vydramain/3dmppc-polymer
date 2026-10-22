#pragma once

// Entity world of the scene ECS: numbered slots with generations, the scene loads, and the registries of entity types,
// component factories and frame systems. Component storage is in rv_ecs_store.hpp; system bodies are in other headers.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

// Full header, not a forward declaration: ecs_world holds rv_cg_camera by value.
#include "pdk/cg/rv_cg.h"
#include "pdk/rv_err.h"

// Defined in pdk/cd/rv_cd.h; only a reference is taken here.
struct rv_cd_scene_component;

namespace rv_pdkkit
{

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
    uint32_t index = 0;      // slot number in ecs_world::slots
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
using ecs_component_factory = int (*)(ecs_world &, ecs_id, const rv_cd_scene_component &);

// Frame system: returns RV_OK or RV_ERR_*. Run by ecs_world_frame in registration order.
using ecs_system = int (*)(ecs_world &, const ecs_frame &);

// One scene load: its number and its entities in file order.
struct ecs_load {
    uint32_t number = 0;
    std::vector<ecs_id> made;
};

// Only the ecs_* functions touch these fields.
struct ecs_world {
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
};

// Index for the next entity: the newest free slot, else a new slot appended.
// Precondition: the caller has checked that an index is left (ecs_entity_create does).
// Throws only before any state changes (vector growth).
inline uint32_t ecs_take_slot(ecs_world &world)
{
    if (world.free_slots.empty()) {
        world.slots.push_back(ecs_slot{});
        return static_cast<uint32_t>(world.slots.size() - 1);
    }
    const uint32_t index = world.free_slots.back();
    world.free_slots.pop_back();
    return index;
}

// Frees a destroyed slot for reuse. A slot at ecs_generation_max stays retired.
// The free-list push may throw, so it runs before the generation changes.
inline void ecs_release_slot(ecs_world &world, uint32_t index)
{
    ecs_slot &slot = world.slots[index];
    const bool reusable = slot.generation != ecs_generation_max;
    if (reusable) {
        world.free_slots.push_back(index);
        ++slot.generation;
    }
}

// RV_ERR_NOMEM when every index is taken.
inline int ecs_entity_create(ecs_world &world, ecs_id &out)
{
    if (world.free_slots.empty() && world.slots.size() >= ecs_slot_limit) {
        return RV_ERR_NOMEM;
    }
    const uint32_t index = ecs_take_slot(world);
    ecs_slot &slot = world.slots[index];
    slot.live = true;
    ++world.live_count;
    out = ecs_id{ index, slot.generation };
    return RV_OK;
}

inline bool ecs_entity_alive(const ecs_world &world, ecs_id id)
{
    if (id.index >= world.slots.size()) {
        return false;
    }
    const ecs_slot &slot = world.slots[id.index];
    return slot.live && slot.generation == id.generation;
}

// RV_ERR_NOENT for an unknown or stale id. Drops the entity's components from every pool.
inline int ecs_entity_destroy(ecs_world &world, ecs_id id)
{
    if (!ecs_entity_alive(world, id)) {
        return RV_ERR_NOENT;
    }
    ecs_release_slot(world, id.index);
    for (const std::unique_ptr<ecs_pool_base> &pool : world.pools) {
        if (pool) {
            pool->drop(id);
        }
    }
    ecs_slot &slot = world.slots[id.index];
    slot.live = false;
    --world.live_count;
    return RV_OK;
}

inline uint32_t ecs_world_count(const ecs_world &world)
{
    return world.live_count;
}

} // namespace rv_pdkkit
