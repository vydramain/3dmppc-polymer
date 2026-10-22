#pragma once

// The records of a form-2 scene document, flattened from its parsed form. A disc builds them here, so its loader needs no
// console. Reading the asset and migrating it to form 2 is ecs_scene_records_read in rv_ecs_scene.hpp.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdkkit/scene/rv_scene.hpp"

namespace rv_pdkkit
{

inline constexpr const char *ecs_scene_records_tag = "ecs-scene";

// One object of a scene: its id and parent, the only fields of form 2. Its components are in ecs_scene_records::components.
struct ecs_scene_object {
    char id[ecs_name_capacity]; // NUL-terminated
    int64_t parent; // index of the parent object, -1 for a root
};

// One field of a component, as text. Key is NUL-terminated; value keeps the text rv_scene_render writes: strings in
// double quotes, arrays as [a, b, c], numbers in their shortest fixed form. The quotes tell a string from a number.
struct ecs_scene_field {
    char key[ecs_name_capacity];
    char value[ecs_value_capacity];
};

// A component of a scene, flattened over its objects in document order; only the first field_count fields are in use.
struct ecs_scene_component {
    int64_t object; // index of the object the component belongs to
    char name[ecs_name_capacity];
    int64_t field_count;
    ecs_scene_field fields[ecs_component_field_capacity];
};

// The entity type of an object, NUL-terminated; empty means the base type.
struct ecs_scene_entity {
    char type[ecs_name_capacity];
};

// One record per object: id, parent and entity in file order; components flattened over the scene in document order.
struct ecs_scene_records {
    std::vector<ecs_scene_object> objects;
    std::vector<ecs_scene_entity> entities;
    std::vector<ecs_scene_component> components;
};

// Copies `value` into `dst` of `capacity` bytes, NUL included. RV_ERR_INVAL when it does not fit or holds a NUL
// byte: a C reader would see the text cut short.
inline int ecs_scene_copy_text(const std::string &value, char *dst, std::size_t capacity)
{
    if (value.size() >= capacity || value.find('\0') != std::string::npos) {
        return RV_ERR_INVAL;
    }
    std::memcpy(dst, value.data(), value.size());
    dst[value.size()] = '\0';
    return RV_OK;
}

// The id and parent of `object` into `out`. An id that does not fit or holds a NUL refuses the scene.
inline int ecs_scene_object_fill(const std::string &scene, const rv_scene_object &object, ecs_scene_object &out)
{
    const int copied = ecs_scene_copy_text(object.id, out.id, ecs_name_capacity);
    if (copied != RV_OK) {
        RV_LOG_ERR(ecs_scene_records_tag,
            "scene '{}' refused: object id is longer than {} bytes or with a NUL",
            rv_pdklib::rv_log_escape(scene.c_str()),
            ecs_name_capacity - 1);
        return copied;
    }
    out.parent = object.parent;
    return RV_OK;
}

// The entity type of `object` into `out`; a type that does not fit or holds a NUL refuses the scene.
inline int ecs_scene_entity_fill(const std::string &scene, const rv_scene_object &object, ecs_scene_entity &out)
{
    const int copied = ecs_scene_copy_text(object.entity, out.type, ecs_name_capacity);
    if (copied != RV_OK) {
        RV_LOG_ERR(ecs_scene_records_tag,
            "scene '{}' refused: object '{}' entity type is longer than {} bytes or with a NUL",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(object.id.c_str()),
            ecs_name_capacity - 1);
        return copied;
    }
    return RV_OK;
}

// One field of component `component` into `out`: key and value as text. Either one that does not fit refuses the scene.
inline int ecs_scene_field_fill(const std::string &scene,
    const std::string &component,
    const rv_pdklib::rv_manifest_tree_entry &entry,
    ecs_scene_field &out)
{
    const int key = ecs_scene_copy_text(entry.key, out.key, ecs_name_capacity);
    if (key != RV_OK) {
        RV_LOG_ERR(ecs_scene_records_tag,
            "scene '{}' refused: component '{}' field key is longer than {} bytes or with a NUL",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(component.c_str()),
            ecs_name_capacity - 1);
        return key;
    }
    const std::string value = rv_scene_value_text(entry.value);
    const int copied = ecs_scene_copy_text(value, out.value, ecs_value_capacity);
    if (copied != RV_OK) {
        RV_LOG_ERR(ecs_scene_records_tag,
            "scene '{}' refused: component '{}' field '{}' value is longer than {} bytes or with a NUL",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(component.c_str()),
            rv_pdklib::rv_log_escape(entry.key.c_str()),
            ecs_value_capacity - 1);
        return copied;
    }
    return RV_OK;
}

// `component` of `loaded` into `out`: its object, name and fields as text. Too many fields, or a name, key or
// value that does not fit or holds a NUL, refuses the scene.
inline int ecs_scene_component_fill(const std::string &scene,
    const rv_scene &loaded,
    const rv_scene_component &component,
    ecs_scene_component &out)
{
    const int64_t field_count = static_cast<int64_t>(component.fields.size());
    if (field_count > ecs_component_field_capacity) {
        RV_LOG_ERR(ecs_scene_records_tag,
            "scene '{}' refused: component '{}' has {} fields, the limit is {}",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(component.name.c_str()),
            field_count,
            ecs_component_field_capacity);
        return RV_ERR_INVAL;
    }
    const int named = ecs_scene_copy_text(component.name, out.name, ecs_name_capacity);
    if (named != RV_OK) {
        // The parser resolves component.object to an index of loaded.objects.
        RV_LOG_ERR(ecs_scene_records_tag,
            "scene '{}' refused: component of object '{}' has a name longer than {} bytes or with a NUL",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(loaded.objects[static_cast<std::size_t>(component.object)].id.c_str()),
            ecs_name_capacity - 1);
        return named;
    }
    out.object = component.object;
    out.field_count = field_count;
    for (std::size_t i = 0; i < component.fields.size(); ++i) {
        const int field = ecs_scene_field_fill(scene, component.name, component.fields[i], out.fields[i]);
        if (field != RV_OK) {
            return field;
        }
    }
    return RV_OK;
}

// Flattens `loaded`, a form-2 document, into `out`: one id, parent and entity per object, then the components.
inline int ecs_scene_flatten(const std::string &scene, const rv_scene &loaded, ecs_scene_records &out)
{
    for (std::size_t i = 0; i < loaded.objects.size(); ++i) {
        ecs_scene_object object{};
        const int filled = ecs_scene_object_fill(scene, loaded.objects[i], object);
        if (filled != RV_OK) {
            return filled;
        }
        ecs_scene_entity entity{};
        const int typed = ecs_scene_entity_fill(scene, loaded.objects[i], entity);
        if (typed != RV_OK) {
            return typed;
        }
        out.objects.push_back(object);
        out.entities.push_back(entity);
    }
    for (const rv_scene_component &component : loaded.components) {
        ecs_scene_component flat{};
        const int filled = ecs_scene_component_fill(scene, loaded, component, flat);
        if (filled != RV_OK) {
            return filled;
        }
        out.components.push_back(flat);
    }
    return RV_OK;
}

} // namespace rv_pdkkit
