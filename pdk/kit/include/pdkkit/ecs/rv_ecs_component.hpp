#pragma once

// Built-in components of the scene ECS, and the registry of factories that fill them from a scene's [[component]] fields.

#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>

#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_scene_records.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace rv_pdkkit
{

// Default tint: white, no change of the model's colours.
inline constexpr rv_color ecs_tint_white{ 255, 255, 255 };

// Position, rotation (Q20.12 degrees) and scale relative to the entity's parent in the hierarchy (ecs_component_parent);
// a world root is relative to the world.
struct ecs_component_transform {
    rv_pdklib::fx_vec3 position{};
    rv_pdklib::fx_vec3 rotation{};
    rv_pdklib::fx_vec3 scale{ rv_pdklib::fx_one, rv_pdklib::fx_one, rv_pdklib::fx_one };
};

// The entity's parent in the hierarchy; an entity without it is a root of the world.
struct ecs_component_parent {
    ecs_id parent{};
};

// Entity name from the scene. A char array keeps the component a POD that a description can fill. NUL-terminated,
// empty by default; sized as ecs_value_capacity (ecs_scene_copy_name static_asserts it holds a whole scene name).
struct ecs_component_name {
    char text[ecs_value_capacity]{};
};

// A .mppcmod model on the disc, with its tint. name is NUL-terminated, empty by default; sized as ecs_value_capacity,
// like ecs_component_name::text.
struct ecs_component_model {
    char name[ecs_value_capacity]{};
    rv_color tint = ecs_tint_white;
};

// Marks the entity as a camera.
struct ecs_component_camera {};
// Marks a scene object of kind "volume" (drawn as a wireframe in the editor).
struct ecs_component_volume {};
// Marks the entity as not drawn.
struct ecs_component_hidden {};
// Marks the entity as a decal.
struct ecs_component_decal {};
// Marks the entity as drawn without back-face culling, the flag covers the whole model. A flat card is seen from
// both faces as one layer of quads, not as a back-to-back pair.
struct ecs_component_two_sided {};

// Component of id, added with its defaults when missing. out is written only on success. RV_ERR_NOENT for a dead or
// stale id, otherwise the code of the add (RV_ERR_NOMEM when the pool cannot grow), RV_ERR_INVAL for any other
// exception (ecs_guard). Nothing is added on failure.
template <typename T>
[[nodiscard]] int ecs_component_ensure(ecs_world &world, ecs_id id, T *&out)
{
    return ecs_guard([&]() -> int {
        if (!ecs_entity_alive(world, id)) {
            return RV_ERR_NOENT;
        }
        if (ecs_component_get<T>(world, id) == nullptr) {
            const int added = ecs_component_add<T>(world, id);
            if (added != RV_OK) {
                return added;
            }
        }
        out = ecs_component_get<T>(world, id);
        return RV_OK;
    });
}

// Field with the key, or nullptr. Only the first field_count fields are in use.
inline const ecs_scene_field *ecs_field_find(const ecs_scene_component &component, std::string_view key)
{
    const int64_t capacity = ecs_component_field_capacity;
    const int64_t count = component.field_count < capacity ? component.field_count : capacity;
    for (int64_t i = 0; i < count; ++i) {
        const ecs_scene_field &field = component.fields[i];
        if (std::string_view{ field.key, ecs_name_length(field.key) } == key) {
            return &field;
        }
    }
    return nullptr;
}

// RV_ERR_NOENT for a missing key, RV_ERR_INVAL when the whole value is not an integer. out is written only on success.
inline int ecs_field_int(const ecs_scene_component &component, std::string_view key, int64_t &out)
{
    const ecs_scene_field *field = ecs_field_find(component, key);
    if (field == nullptr) {
        return RV_ERR_NOENT;
    }
    const std::string_view text{ field->value, ecs_name_length(field->value) };
    const char *end = text.data() + text.size();
    int64_t value = 0;
    const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
    const bool whole = parsed.ec == std::errc{} && parsed.ptr == end;
    if (!whole) {
        return RV_ERR_INVAL;
    }
    out = value;
    return RV_OK;
}

// Factory of a marker: it has no fields, so any field is RV_ERR_INVAL. An existing marker is kept, not an error.
template <typename T>
int ecs_marker_factory(ecs_world &world, ecs_id id, const ecs_scene_component &component)
{
    const bool fielded = component.field_count != 0;
    if (fielded) {
        return RV_ERR_INVAL;
    }
    T *marker = nullptr;
    return ecs_component_ensure<T>(world, id, marker);
}

inline int ecs_hidden_factory(ecs_world &world, ecs_id id, const ecs_scene_component &component)
{
    return ecs_marker_factory<ecs_component_hidden>(world, id, component);
}

inline int ecs_decal_factory(ecs_world &world, ecs_id id, const ecs_scene_component &component)
{
    return ecs_marker_factory<ecs_component_decal>(world, id, component);
}

inline int ecs_two_sided_factory(ecs_world &world, ecs_id id, const ecs_scene_component &component)
{
    return ecs_marker_factory<ecs_component_two_sided>(world, id, component);
}

// Factory of the name, or nullptr. The empty name is never registered.
inline ecs_component_factory ecs_component_find(const ecs_world &world, std::string_view name)
{
    const auto &state = detail::ecs_state(world);
    for (const auto &entry : state.factories) {
        if (entry.first == name) {
            return entry.second;
        }
    }
    return nullptr;
}

// RV_ERR_INVAL for an empty name or a null factory, RV_ERR_ALREADY when the name is taken, RV_ERR_NOMEM when the
// registry cannot grow.
[[nodiscard]] inline int ecs_component_register(ecs_world &world, std::string_view name, ecs_component_factory make)
{
    auto &state = detail::ecs_state(world);
    return ecs_guard([&] {
        const bool bad = name.empty() || make == nullptr;
        if (bad) {
            return RV_ERR_INVAL;
        }
        if (ecs_component_find(world, name) != nullptr) {
            return RV_ERR_ALREADY;
        }
        state.factories.emplace_back(name, make);
        return RV_OK;
    });
}

} // namespace rv_pdkkit
