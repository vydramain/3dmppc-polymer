#pragma once

// The internals of the object accessors, for rv_editor_scene_object.cpp and rv_editor_scene_object_fx.cpp only.

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <string_view>

#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdkkit/scene/rv_scene.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "scene/rv_editor_scene.hpp"

namespace rv_editor
{

// Vector component count (x, y, z).
inline constexpr size_t vec3_size = 3;
// UV coordinate component count (u, v, u2, v2).
inline constexpr size_t uv_size = 4;

// Reasons the sprite and tess refusals give; each names what was refused, after the object and field.
inline constexpr std::string_view reason_no_sprite = "has no quad or billboard component";
inline constexpr std::string_view reason_tess_positive = "must be a number greater than zero";

// A component's name and its field at `index`, in declaration order, from its ECS description.
template <typename T>
inline std::string component_name_of()
{
    return rv_pdkkit::ecs_component_traits<T>::desc.name;
}

template <typename T>
inline std::string component_field_of(std::size_t index)
{
    return rv_pdkkit::ecs_component_traits<T>::desc.fields[index].name;
}

// The component and field names the accessors use: pdkkit's descriptions, and the sprites' components, which
// are not in the ECS table, so their names and keys are literal here.
struct scene_names {
    std::string name = component_name_of<rv_pdkkit::ecs_component_name>();
    std::string name_text = component_field_of<rv_pdkkit::ecs_component_name>(0);
    std::string transform = component_name_of<rv_pdkkit::ecs_component_transform>();
    std::string position = component_field_of<rv_pdkkit::ecs_component_transform>(0);
    std::string rotation = component_field_of<rv_pdkkit::ecs_component_transform>(1);
    std::string scale = component_field_of<rv_pdkkit::ecs_component_transform>(2);
    std::string camera = component_name_of<rv_pdkkit::ecs_component_camera>();
    std::string volume = component_name_of<rv_pdkkit::ecs_component_volume>();
    std::string model = component_name_of<rv_pdkkit::ecs_component_model>();
    std::string model_source = component_field_of<rv_pdkkit::ecs_component_model>(0);
    std::string model_tint = component_field_of<rv_pdkkit::ecs_component_model>(1);
    std::string legacy_quad = "legacy_quad";
    std::string legacy_billboard = "legacy_billboard";
    std::string legacy_texture = "texture";
    std::string legacy_uv = "uv";
    std::string legacy_tint = "tint";
    std::string legacy_tess = "tess";
};

inline const scene_names &names()
{
    static const scene_names n{};
    return n;
}

// A refused edit leaves the field as it was and logs why, naming the object and the field.
inline void refuse(const rv_editor_scene_object &o, std::string_view field, std::string_view why)
{
    RV_LOG_WARN(scene_log_tag, "object '{}': '{}' {}, not changed", o.id, field, why);
}

// The object's first component named one of wanted, or nullptr.
template <typename Object>
auto find_first(Object &o, std::initializer_list<std::string_view> wanted) -> decltype(&o.components.front())
{
    for (auto &c : o.components) {
        if (std::ranges::find(wanted, c.name) != wanted.end()) {
            return &c;
        }
    }
    return nullptr;
}

// The sprite's component (quad or billboard), or nullptr.
template <typename Object>
auto sprite_of(Object &o) -> decltype(&o.components.front())
{
    return find_first(o, { names().legacy_quad, names().legacy_billboard });
}

// The accessors speak fx_vec3; the field helpers take arrays.
inline std::array<rv_pdklib::fx, vec3_size> array_of(const rv_pdklib::fx_vec3 &v)
{
    return { v.x, v.y, v.z };
}

} // namespace rv_editor
