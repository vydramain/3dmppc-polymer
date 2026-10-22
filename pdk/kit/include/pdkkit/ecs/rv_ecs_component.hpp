#pragma once

// Built-in components of the scene ECS, and the registry of factories that fill them from a scene's [[component]] fields.

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

#include "pdk/cd/rv_cd.h"
#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace rv_pdklib
{

// Default tint: white, no change of the model's or sprite's colours.
inline constexpr rv_color ecs_tint_white{ 255, 255, 255 };
// Default sprite tessellation: two subdivisions per unit, as the editor's scene.
inline constexpr fx ecs_sprite_tess_default = 2 * fx_one;

// Position, rotation (Q20.12 degrees) and scale relative to the parent; parent {} is the scene root.
struct ecs_component_transform {
    fx_vec3 position{};
    fx_vec3 rotation{};
    fx_vec3 scale{ fx_one, fx_one, fx_one };
    ecs_id parent{};
};

// Entity name from the scene.
struct ecs_component_name {
    std::string text;
};

// A .mppcmod model on the disc, with its tint.
struct ecs_component_model {
    std::string name;
    rv_color tint = ecs_tint_white;
};

enum class ecs_sprite_kind : uint8_t {
    quad,
    billboard,
};

// A textured quad or billboard; uv holds u0, v0, u1, v1 in pixels.
struct ecs_component_sprite {
    ecs_sprite_kind kind = ecs_sprite_kind::quad;
    std::string texture;
    fx uv[4]{};
    rv_color tint = ecs_tint_white;
    fx tess = ecs_sprite_tess_default;
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

// Component of id, added with its defaults when missing. nullptr for a stale id.
// The add fails only for a dead id or a held T, and both are excluded above. Growth of the pool may throw
// std::bad_alloc before any state changes.
template <typename T>
T *ecs_component_ensure(ecs_world &world, ecs_id id)
{
    if (!ecs_entity_alive(world, id)) {
        return nullptr;
    }
    if (ecs_component_get<T>(world, id) == nullptr) {
        ecs_component_add<T>(world, id);
    }
    return ecs_component_get<T>(world, id);
}

// Field with the key, or nullptr. Only the first field_count fields are in use.
inline const rv_cd_scene_field *ecs_field_find(const rv_cd_scene_component &component, std::string_view key)
{
    const int64_t capacity = RV_CD_COMPONENT_FIELD_CAPACITY;
    const int64_t count = component.field_count < capacity ? component.field_count : capacity;
    for (int64_t i = 0; i < count; ++i) {
        const rv_cd_scene_field &field = component.fields[i];
        if (std::string_view{ field.key, ecs_name_length(field.key) } == key) {
            return &field;
        }
    }
    return nullptr;
}

// RV_ERR_NOENT for a missing key, RV_ERR_INVAL when the whole value is not an integer. out is written only on success.
inline int ecs_field_int(const rv_cd_scene_component &component, std::string_view key, int64_t &out)
{
    const rv_cd_scene_field *field = ecs_field_find(component, key);
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
int ecs_marker_factory(ecs_world &world, ecs_id id, const rv_cd_scene_component &component)
{
    const bool fielded = component.field_count != 0;
    if (fielded) {
        return RV_ERR_INVAL;
    }
    if (ecs_component_ensure<T>(world, id) == nullptr) {
        return RV_ERR_NOENT;
    }
    return RV_OK;
}

inline int ecs_hidden_factory(ecs_world &world, ecs_id id, const rv_cd_scene_component &component)
{
    return ecs_marker_factory<ecs_component_hidden>(world, id, component);
}

inline int ecs_decal_factory(ecs_world &world, ecs_id id, const rv_cd_scene_component &component)
{
    return ecs_marker_factory<ecs_component_decal>(world, id, component);
}

inline int ecs_two_sided_factory(ecs_world &world, ecs_id id, const rv_cd_scene_component &component)
{
    return ecs_marker_factory<ecs_component_two_sided>(world, id, component);
}

// Factory of the name, or nullptr. The empty name is never registered.
inline ecs_component_factory ecs_component_find(const ecs_world &world, std::string_view name)
{
    for (const auto &entry : world.factories) {
        if (entry.first == name) {
            return entry.second;
        }
    }
    return nullptr;
}

// RV_ERR_INVAL for an empty name or a null factory, RV_ERR_ALREADY when the name is taken.
inline int ecs_component_register(ecs_world &world, std::string_view name, ecs_component_factory make)
{
    const bool bad = name.empty() || make == nullptr;
    if (bad) {
        return RV_ERR_INVAL;
    }
    if (ecs_component_find(world, name) != nullptr) {
        return RV_ERR_ALREADY;
    }
    world.factories.emplace_back(name, make);
    return RV_OK;
}

} // namespace rv_pdklib
