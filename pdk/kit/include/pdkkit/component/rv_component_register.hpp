#pragma once

// The factory and the registration of a component type. The description comes from rv_component_macro.hpp:
// PDKKIT_COMPONENT gives T its ecs_component_desc, ecs_component_factory_of<T> fills T from a scene's [[component]]
// fields, and ecs_component_register<T> puts both in the world's registry.

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdkkit/component/rv_component_desc.hpp"
#include "pdkkit/component/rv_component_field_text.hpp"
#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_scene_records.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_pdkkit
{

// Log tag of the factories' lines.
inline constexpr const char *ecs_component_tag = "ecs-component";
// Reasons of the factories' log lines; the caller's text follows the component and field names.
inline constexpr const char *ecs_err_unknown_field = "is not a field of the description, so it is skipped";
inline constexpr const char *ecs_err_repeated = "is repeated in the scene";
inline constexpr const char *ecs_err_add =
    "is not added: the id is stale, the entity has it already, the pool cannot grow, or the add threw";
// Reason of a built-in component that ecs_builtins_register refuses; the refusal's code is returned to the caller.
inline constexpr const char *ecs_err_builtin = "is a built-in component and is not registered";

// True when T has `static int ecs_check(const T &, std::string &reason)`.
template <typename T>
concept ecs_component_checked = requires(const T &value, std::string &reason) {
    { T::ecs_check(value, reason) } -> std::same_as<int>;
};

// True when T has `static int ecs_derive(T &, std::string &reason)`.
template <typename T>
concept ecs_component_derived = requires(T &value, std::string &reason) {
    { T::ecs_derive(value, reason) } -> std::same_as<int>;
};

// Best effort, as ecs_guard_log: a line whose formatting throws is lost, and the factory's code stands.
inline void ecs_component_log_warn(const char *component, const char *field, const char *reason)
{
    try {
        RV_LOG_WARN(ecs_component_tag,
            "component '{}' field '{}' {}",
            rv_pdklib::rv_log_escape(component),
            rv_pdklib::rv_log_escape(field),
            reason);
    } catch (...) {
        // the line is lost
    }
}

// The field is named when given; a null field names the component only.
inline void ecs_component_log_err(const char *component, const char *field, const char *reason)
{
    try {
        if (field == nullptr) {
            RV_LOG_ERR(ecs_component_tag, "component '{}' {}", rv_pdklib::rv_log_escape(component), reason);
        } else {
            RV_LOG_ERR(ecs_component_tag,
                "component '{}' field '{}' {}",
                rv_pdklib::rv_log_escape(component),
                rv_pdklib::rv_log_escape(field),
                reason);
        }
    } catch (...) {
        // the line is lost
    }
}

// The description field named key, or nullptr.
inline const ecs_field_desc *ecs_desc_field_find(const ecs_component_desc &desc, std::string_view key)
{
    for (int64_t i = 0; i < desc.field_count; ++i) {
        const ecs_field_desc &field = desc.fields[i];
        if (std::string_view{ field.name, ecs_name_length(field.name) } == key) {
            return &field;
        }
    }
    return nullptr;
}

// True when an earlier field of the scene component has field i's key.
inline bool ecs_scene_key_repeated(const ecs_scene_component &component, int64_t i)
{
    const ecs_scene_field &field = component.fields[i];
    const std::string_view key{ field.key, ecs_name_length(field.key) };
    for (int64_t j = 0; j < i; ++j) {
        const ecs_scene_field &earlier = component.fields[j];
        if (std::string_view{ earlier.key, ecs_name_length(earlier.key) } == key) {
            return true;
        }
    }
    return false;
}

// Parses one scene field into its place in value. An unknown key is skipped with a warning; a bad value is refused
// with its reason. value is written only for a field that parses.
template <typename T>
int ecs_component_field_set(const ecs_component_desc &desc, const ecs_scene_field &field, T &value)
{
    const std::string_view key{ field.key, ecs_name_length(field.key) };
    const ecs_field_desc *target = ecs_desc_field_find(desc, key);
    if (target == nullptr) {
        ecs_component_log_warn(desc.name, field.key, ecs_err_unknown_field);
        return RV_OK;
    }
    const std::string_view text{ field.value, ecs_name_length(field.value) };
    std::string error;
    auto *bytes = reinterpret_cast<unsigned char *>(&value);
    const int parsed = ecs_field_parse(target->type, text, bytes + target->offset, error);
    if (parsed != RV_OK) {
        ecs_component_log_err(desc.name, field.key, error.c_str());
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Parses every scene field into value, stopping at the first refusal. A key repeated in the scene is refused.
template <typename T>
int ecs_component_fields_set(const ecs_component_desc &desc, const ecs_scene_component &component, T &value)
{
    const int64_t count = std::min<int64_t>(component.field_count, ecs_component_field_capacity);
    for (int64_t i = 0; i < count; ++i) {
        if (ecs_scene_key_repeated(component, i)) {
            ecs_component_log_err(desc.name, component.fields[i].key, ecs_err_repeated);
            return RV_ERR_INVAL;
        }
        const int set = ecs_component_field_set(desc, component.fields[i], value);
        if (set != RV_OK) {
            return set;
        }
    }
    return RV_OK;
}

// Writes the description's defaults into value. A refusal is logged with its reason.
template <typename T>
int ecs_component_defaults(const ecs_component_desc &desc, T &value)
{
    std::string error;
    const int written = ecs_component_write_defaults(desc, reinterpret_cast<unsigned char *>(&value), sizeof(T), error);
    if (written != RV_OK) {
        ecs_component_log_err(desc.name, nullptr, error.c_str());
    }
    return written;
}

// Writes the value a factory starts from: a copy of the entity's own T when it has one, else the description's defaults.
template <typename T>
int ecs_component_base(const ecs_component_desc &desc, const T *existing, T &value)
{
    if (existing != nullptr) {
        value = *existing;
        return RV_OK;
    }
    return ecs_component_defaults(desc, value);
}

// Runs T::ecs_check when T has one. A refusal is logged with its reason and returns RV_ERR_INVAL.
template <typename T>
int ecs_component_check(const ecs_component_desc &desc, const T &value)
{
    if constexpr (ecs_component_checked<T>) {
        std::string reason;
        const int checked = T::ecs_check(value, reason);
        if (checked != RV_OK) {
            ecs_component_log_err(desc.name, nullptr, reason.c_str());
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Runs T::ecs_derive when T has one, to fill the members computed from the described fields. A refusal is logged with
// its reason and returns RV_ERR_INVAL.
template <typename T>
int ecs_component_derive(const ecs_component_desc &desc, T &value)
{
    if constexpr (ecs_component_derived<T>) {
        std::string reason;
        const int derived = T::ecs_derive(value, reason);
        if (derived != RV_OK) {
            ecs_component_log_err(desc.name, nullptr, reason.c_str());
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// The factory of T, with the signature of ecs_component_factory. A local T starts from the entity's own T when it has one
// (an entity type's add_defaults), else from the defaults; then the scene's fields, then ecs_check, then ecs_derive.
// Only a value that passed all of them is written: into the existing T, or added when there is none. A refusal leaves
// the entity as it was.
template <typename T>
int ecs_component_factory_of(ecs_world &world, ecs_id id, const ecs_scene_component &component)
{
    static_assert(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>,
        "a component type must be standard-layout and trivially copyable");
    return ecs_guard([&]() -> int {
        const ecs_component_desc &desc = ecs_component_traits<T>::desc;
        T *existing = ecs_component_get<T>(world, id);
        T value{};
        const int based = ecs_component_base(desc, existing, value);
        if (based != RV_OK) {
            return based;
        }
        const int set = ecs_component_fields_set(desc, component, value);
        if (set != RV_OK) {
            return set;
        }
        const int checked = ecs_component_check(desc, value);
        if (checked != RV_OK) {
            return checked;
        }
        const int derived = ecs_component_derive(desc, value);
        if (derived != RV_OK) {
            return derived;
        }
        if (existing != nullptr) {
            *existing = value;
            return RV_OK;
        }
        const int added = ecs_component_add<T>(world, id, value);
        if (added != RV_OK) {
            ecs_component_log_err(desc.name, nullptr, ecs_err_add);
        }
        return added;
    });
}

// Registers T under the name of its description. The registry keeps a view of that name; traits<T>::desc is a static
// constexpr object, so the name outlives the world.
template <typename T>
[[nodiscard]] int ecs_component_register(ecs_world &world)
{
    const ecs_component_desc &desc = ecs_component_traits<T>::desc;
    const std::string_view name{ desc.name, ecs_name_length(desc.name) };
    return ecs_component_register(world, name, ecs_component_factory_of<T>);
}

// Registers T as ecs_component_register does; a refusal is logged with its reason and the code is returned.
template <typename T>
int ecs_builtin_register(ecs_world &world)
{
    const int code = ecs_component_register<T>(world);
    if (code != RV_OK) {
        ecs_component_log_err(ecs_component_traits<T>::desc.name, nullptr, ecs_err_builtin);
    }
    return code;
}

// Registers the types in order, stopping at the first refusal, and returns its code.
template <typename... T>
int ecs_builtins_register_list(ecs_world &world)
{
    int code = RV_OK;
    static_cast<void>((((code = ecs_builtin_register<T>(world)) == RV_OK) && ...));
    return code;
}

// Registers the built-in components of rv_ecs_component.hpp, the ones with a PDKKIT_COMPONENT description: not
// ecs_component_parent, which the loader sets from an object's parent. A disc calls it before its own
// ecs_component_register calls; a disc that registers one of these names itself gets RV_ERR_ALREADY from that call.
[[nodiscard]] inline int ecs_builtins_register(ecs_world &world)
{
    return ecs_builtins_register_list<ecs_component_name,
        ecs_component_transform,
        ecs_component_model,
        ecs_component_camera,
        ecs_component_volume,
        ecs_component_hidden,
        ecs_component_decal,
        ecs_component_two_sided,
        ecs_component_face_camera>(world);
}

} // namespace rv_pdkkit
