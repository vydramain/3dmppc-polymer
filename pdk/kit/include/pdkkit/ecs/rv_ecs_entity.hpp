#pragma once

// Entity types of the scene ECS. A type names itself and adds its default components to a new entity.
// Per-frame behaviour belongs to systems, not to types.

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"

namespace rv_pdkkit
{

// Length of a name in a char array: up to the first NUL, never past the array.
template <std::size_t N>
std::size_t ecs_name_length(const char (&text)[N])
{
    const char *end = std::find(text, text + N, '\0');
    return static_cast<std::size_t>(end - text);
}

// Base of entity types. Derived types pass their name as a literal; the base passes "".
struct ecs_entity {
    std::string_view name;

    // Takes a literal or a char array holding the name and a terminating NUL. Without a NUL the name ends at the array end.
    template <std::size_t N>
    explicit ecs_entity(const char (&type_name)[N])
        : name(type_name, ecs_name_length(type_name))
    {
    }

    virtual ~ecs_entity() = default;

    // Adds the default components to a just created entity. It must not destroy that entity.
    // It adds components to id only: it creates and destroys no other entity.
    // The failure code is returned as is.
    virtual int add_defaults(ecs_world &, ecs_id) const
    {
        return RV_OK;
    }
};

// The type of an entity with an empty name in a scene.
inline const ecs_entity &ecs_entity_base()
{
    static const ecs_entity base("");
    return base;
}

// Registered type of the name, or nullptr. The empty name is never registered.
inline const ecs_entity *ecs_entity_find(const ecs_world &world, std::string_view name)
{
    const auto &state = detail::ecs_state(world);
    for (const ecs_entity *type : state.types) {
        if (type->name == name) {
            return type;
        }
    }
    return nullptr;
}

// RV_ERR_INVAL for an empty name, RV_ERR_ALREADY when the name is taken, RV_ERR_NOMEM when the registry cannot grow.
// The type must outlive the world.
[[nodiscard]] inline int ecs_entity_register(ecs_world &world, const ecs_entity &type)
{
    auto &state = detail::ecs_state(world);
    return ecs_guard([&] {
        if (type.name.empty()) {
            return RV_ERR_INVAL;
        }
        if (ecs_entity_find(world, type.name) != nullptr) {
            return RV_ERR_ALREADY;
        }
        state.types.push_back(&type);
        return RV_OK;
    });
}

// A temporary type would leave the registry with a dangling pointer.
inline int ecs_entity_register(ecs_world &world, const ecs_entity &&type) = delete;

// Puts the world back after a create that hands out no id. The slot returns to the free list as it was: a reused
// slot keeps its generation, an appended slot is popped. The free-list push cannot allocate: the caller reserved it.
// Observably the world is as before: live entities, slots, generations and pool elements. A new type's empty pool
// and a grown pool sparse may stay (E7c).
inline void ecs_rollback_create(ecs_world &world, ecs_id id, std::size_t slots_before)
{
    auto &state = detail::ecs_state(world);
    const bool appended = state.slots.size() > slots_before;
    const uint32_t generation = appended ? ecs_first_generation : id.generation;
    (void)ecs_entity_destroy(world, id); // drops the components; NOENT when add_defaults destroyed id already
    state.slots[id.index].generation = generation;
    if (appended) {
        state.free_slots.pop_back();
        state.slots.pop_back();
    }
}

// RV_ERR_NOENT for an unknown name, RV_ERR_NOMEM when no index is left or a growth throws bad_alloc, the failed
// default add's code, or RV_ERR_INVAL when add_defaults destroyed the entity. Only success writes out. A default
// add that fails or throws leaves no entity behind: the world is rolled back, and a throw becomes its code.
[[nodiscard]] inline int ecs_entity_create_as(ecs_world &world, std::string_view name, ecs_id &out)
{
    auto &state = detail::ecs_state(world);
    return ecs_guard([&]() -> int {
        const bool base = name.empty();
        const ecs_entity *type = base ? &ecs_entity_base() : ecs_entity_find(world, name);
        if (type == nullptr) {
            return RV_ERR_NOENT;
        }
        // The rollback pushes id onto free_slots, and that push must not allocate: capacity grows ahead of the slot count.
        const std::size_t needed = state.slots.size() + 1;
        if (state.free_slots.capacity() < needed) {
            state.free_slots.reserve(std::max(needed, ecs_pool_growth_factor * state.free_slots.capacity()));
        }
        const std::size_t slots_before = state.slots.size();
        ecs_id id{};
        const int created = ecs_entity_create(world, id);
        if (created != RV_OK) {
            return created;
        }
        int added = RV_OK;
        try {
            added = type->add_defaults(world, id);
        } catch (...) {
            ecs_rollback_create(world, id, slots_before);
            throw;
        }
        if (added != RV_OK) {
            ecs_rollback_create(world, id, slots_before);
            return added;
        }
        if (!ecs_entity_alive(world, id)) {
            ecs_rollback_create(world, id, slots_before);
            return RV_ERR_INVAL;
        }
        out = id;
        return RV_OK;
    });
}

} // namespace rv_pdkkit
