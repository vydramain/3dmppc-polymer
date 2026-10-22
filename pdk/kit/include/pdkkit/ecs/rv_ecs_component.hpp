#pragma once

// Built-in components of the scene ECS, and the registry of factories that fill them from a scene's [[component]] fields.

#include <charconv>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>

#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_scene_records.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_models/rv_mppcmod.hpp"

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

// Copies the .mppcmod name of source into out, NUL included. RV_ERR_INVAL when the name does not fit out whole; out is
// then untouched.
inline int ecs_model_name_copy(const std::string &source, char (&out)[ecs_value_capacity])
{
    const std::string disc = rv_pdklib::rv_mppcmod_disc_name(source);
    const bool fits = disc.size() < sizeof out;
    if (!fits) {
        return RV_ERR_INVAL;
    }
    std::memset(out, 0, sizeof out);
    std::memcpy(out, disc.data(), disc.size());
    return RV_OK;
}

// A model on the disc and its tint. source is the model's file in assets/models/ ("factory.gltf"), from the scene; name
// is the .mppcmod it has on the disc, derived from source by ecs_derive. Both are NUL-terminated, sized as
// ecs_value_capacity, like ecs_component_name::text.
struct ecs_component_model {
    char source[ecs_value_capacity]{};
    rv_color tint = ecs_tint_white;
    char name[ecs_value_capacity]{};

    // Fills name from source. Refused when source is empty, has no file name, or when its .mppcmod name does not fit name.
    static int ecs_derive(ecs_component_model &model, std::string &reason)
    {
        const std::string source{ model.source, ecs_name_length(model.source) };
        const bool empty = source.empty();
        if (empty) {
            reason = "source is empty: a model needs its file in assets/models/";
            return RV_ERR_INVAL;
        }
        const std::string disc = rv_pdklib::rv_mppcmod_disc_name(source);
        const bool nameless = disc == rv_pdklib::rv_mppcmod_extension;
        if (nameless) {
            reason = "source has no file name";
            return RV_ERR_INVAL;
        }
        const int copied = ecs_model_name_copy(source, model.name);
        if (copied != RV_OK) {
            reason = "the .mppcmod name of source does not fit the name field";
            return RV_ERR_INVAL;
        }
        return RV_OK;
    }
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
// Marks the entity's model to be turned to face the camera, as a billboard. ecs_system_face_camera turns it.
struct ecs_component_face_camera {};

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

// Descriptions of the built-in components; a disc registers each with ecs_component_register<T>.
PDKKIT_COMPONENT(rv_pdkkit::ecs_component_name, "name", PDKKIT_FIELD(text, string, ""));

PDKKIT_COMPONENT(rv_pdkkit::ecs_component_transform,
    "transform",
    PDKKIT_FIELD(position, fixed3, 0, 0, 0),
    PDKKIT_FIELD(rotation, angle3, 0, 0, 0),
    PDKKIT_FIELD(scale, fixed3, rv_pdklib::fx_one, rv_pdklib::fx_one, rv_pdklib::fx_one));

PDKKIT_COMPONENT(rv_pdkkit::ecs_component_camera, "camera");
PDKKIT_COMPONENT(rv_pdkkit::ecs_component_volume, "volume");
PDKKIT_COMPONENT(rv_pdkkit::ecs_component_hidden, "hidden");
PDKKIT_COMPONENT(rv_pdkkit::ecs_component_decal, "decal");
PDKKIT_COMPONENT(rv_pdkkit::ecs_component_two_sided, "two_sided");
PDKKIT_COMPONENT(rv_pdkkit::ecs_component_face_camera, "face_camera");
PDKKIT_COMPONENT(rv_pdkkit::ecs_component_model,
    "model",
    PDKKIT_FIELD(source, model, ""),
    PDKKIT_FIELD(tint, color, 255, 255, 255));
