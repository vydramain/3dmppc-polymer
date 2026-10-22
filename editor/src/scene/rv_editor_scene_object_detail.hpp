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
#include "pdkkit/scene/rv_scene_migrate.hpp"
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

namespace migrate = rv_pdkkit::detail;

// The component and field names the accessors use: pdkkit's descriptions, and the sprites' legacy names as the
// migration writes them.
struct scene_names {
    std::string name = migrate::migrate_name_of<rv_pdkkit::ecs_component_name>();
    std::string name_text = migrate::migrate_field_of<rv_pdkkit::ecs_component_name>(0);
    std::string transform = migrate::migrate_name_of<rv_pdkkit::ecs_component_transform>();
    std::string position = migrate::migrate_field_of<rv_pdkkit::ecs_component_transform>(0);
    std::string rotation = migrate::migrate_field_of<rv_pdkkit::ecs_component_transform>(1);
    std::string scale = migrate::migrate_field_of<rv_pdkkit::ecs_component_transform>(2);
    std::string camera = migrate::migrate_name_of<rv_pdkkit::ecs_component_camera>();
    std::string volume = migrate::migrate_name_of<rv_pdkkit::ecs_component_volume>();
    std::string model = migrate::migrate_name_of<rv_pdkkit::ecs_component_model>();
    std::string model_source = migrate::migrate_field_of<rv_pdkkit::ecs_component_model>(0);
    std::string model_tint = migrate::migrate_field_of<rv_pdkkit::ecs_component_model>(1);
    std::string legacy_quad = migrate::legacy_quad_name;
    std::string legacy_billboard = migrate::legacy_billboard_name;
    std::string legacy_texture = migrate::legacy_texture_key;
    std::string legacy_uv = migrate::legacy_uv_key;
    std::string legacy_tint = migrate::legacy_tint_key;
    std::string legacy_tess = migrate::legacy_tess_key;
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
