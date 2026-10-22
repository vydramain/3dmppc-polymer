#pragma once

// Entity types of the scene ECS. A type names itself and adds its default components to a new entity.
// Per-frame behaviour belongs to systems, not to types.

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"

namespace rv_pdklib
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
    for (const ecs_entity *type : world.types) {
        if (type->name == name) {
            return type;
        }
    }
    return nullptr;
}

// RV_ERR_INVAL for an empty name, RV_ERR_ALREADY when the name is taken. The type must outlive the world.
inline int ecs_entity_register(ecs_world &world, const ecs_entity &type)
{
    if (type.name.empty()) {
        return RV_ERR_INVAL;
    }
    if (ecs_entity_find(world, type.name) != nullptr) {
        return RV_ERR_ALREADY;
    }
    world.types.push_back(&type);
    return RV_OK;
}

// A temporary type would leave the registry with a dangling pointer.
inline int ecs_entity_register(ecs_world &world, const ecs_entity &&type) = delete;

// RV_ERR_NOENT for an unknown name, RV_ERR_NOMEM when no index is left, the failed default add's code,
// or RV_ERR_INVAL when add_defaults destroyed the entity. Only success writes out. A throwing add_defaults
// destroys the entity and rethrows, so the caller never sees a half-made entity.
inline int ecs_entity_create_as(ecs_world &world, std::string_view name, ecs_id &out)
{
    const bool base = name.empty();
    const ecs_entity *type = base ? &ecs_entity_base() : ecs_entity_find(world, name);
    if (type == nullptr) {
        return RV_ERR_NOENT;
    }
    // The catch below frees id, and that free-list push must not allocate: capacity grows ahead of the slot count.
    const std::size_t needed = world.slots.size() + 1;
    if (world.free_slots.capacity() < needed) {
        world.free_slots.reserve(std::max(needed, ecs_pool_growth_factor * world.free_slots.capacity()));
    }
    ecs_id id{};
    const int created = ecs_entity_create(world, id);
    if (created != RV_OK) {
        return created;
    }
    int added = RV_OK;
    try {
        added = type->add_defaults(world, id);
    } catch (...) {
        ecs_entity_destroy(world, id); // alive: add_defaults must not destroy it
        throw;
    }
    if (added != RV_OK) {
        ecs_entity_destroy(world, id); // NOENT when add_defaults already destroyed id: nothing to undo
        return added;
    }
    const bool alive = ecs_entity_alive(world, id);
    if (!alive) {
        return RV_ERR_INVAL;
    }
    out = id;
    return RV_OK;
}

} // namespace rv_pdklib
