#pragma once

// The records of a scene document, built from the asset's bytes as the console's scene reads hold them:
// the same fields, the same refusals. A disc builds them here, so its loader needs no console.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/cv/rv_vertex.h" // rv_color: object tint
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdkkit/scene/rv_scene.hpp"

namespace rv_pdkkit
{

inline constexpr const char *ecs_scene_records_tag = "ecs-scene";

// An authored mesh ".glb" or ".gltf" is served from the disc as ".mppcmod" with the same stem and directory.
inline constexpr std::string_view ecs_scene_mesh_exts[] = { ".glb", ".gltf" };
inline constexpr std::string_view ecs_scene_model_ext = ".mppcmod";

// Bytes per name, NUL included: the arrays of ecs_scene_object, ecs_scene_entity and ecs_scene_component. The
// sizes match the console's scene reads, so a scene the drive serves loads here too.
inline constexpr int ecs_name_capacity = 64;
// Bytes per component field value, NUL included.
inline constexpr int ecs_value_capacity = 64;
// Fields per component, the fixed size of ecs_scene_component: a component with more refuses the scene.
inline constexpr int ecs_component_field_capacity = 8;

// Kinds of objects in a scene: the object kinds of the editor's scene.
enum class ecs_scene_kind {
    group = 1,
    camera,
    mesh,
    quad,
    billboard,
    volume,
};

// One object of a scene. Names are NUL-terminated inside their arrays; an empty model or texture means none.
struct ecs_scene_object {
    char id[ecs_name_capacity];
    char name[ecs_name_capacity];
    int64_t parent; // index of the parent object, -1 for a root
    ecs_scene_kind kind;
    // row-major: element (row, col) is world[row*4+col]; column vectors multiplied
    // from the right, the layout of pdklib's rv_mat4
    float world[16];
    char model[ecs_name_capacity]; // .mppcmod name on the disc
    char texture[ecs_name_capacity];
    float uv[4]; // u0, v0, u1, v1 in pixels; quad and billboard only
    rv_color tint;
    float tess; // quad/billboard subdivision density, as the editor's scene
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

// Local transform as Q20.12 numbers of the scene document, relative to the parent: position x, y, z; rotation pitch,
// yaw, roll in degrees; scale.
struct ecs_scene_local {
    int32_t position[3];
    int32_t rotation[3];
    int32_t scale[3];
};

// The kind words pdklib reads, and the ecs_scene_kind each one is.
struct ecs_scene_kind_entry {
    std::string_view word;
    ecs_scene_kind kind;
};

inline constexpr ecs_scene_kind_entry ecs_scene_kinds[] = {
    { "group", ecs_scene_kind::group },
    { "camera", ecs_scene_kind::camera },
    { "mesh", ecs_scene_kind::mesh },
    { "quad", ecs_scene_kind::quad },
    { "billboard", ecs_scene_kind::billboard },
    { "volume", ecs_scene_kind::volume },
};

// One record per object, entity and local in file order; components flattened over the scene in document order.
struct ecs_scene_records {
    std::vector<ecs_scene_object> objects;
    std::vector<ecs_scene_entity> entities;
    std::vector<ecs_scene_local> locals;
    std::vector<ecs_scene_component> components;
};

// True, with `kind_out` set, when `word` is a kind; false otherwise.
inline bool ecs_scene_kind_of(std::string_view word, ecs_scene_kind &kind_out)
{
    for (const ecs_scene_kind_entry &entry : ecs_scene_kinds) {
        if (entry.word == word) {
            kind_out = entry.kind;
            return true;
        }
    }
    return false;
}

// The disc name of a scene mesh. An empty mesh stays empty; a name ending in neither extension is already a disc name.
inline std::string ecs_scene_model_name(const std::string &mesh)
{
    for (const std::string_view ext : ecs_scene_mesh_exts) {
        if (std::string_view(mesh).ends_with(ext)) {
            return mesh.substr(0, mesh.size() - ext.size()) + std::string(ecs_scene_model_ext);
        }
    }
    return mesh;
}

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

// A drive's negative code as this file's int. Codes are small; one outside int's range is not a code the contract has.
inline int ecs_scene_drive_code(int64_t code)
{
    if (code < std::numeric_limits<int>::min()) {
        return RV_ERR_INVAL;
    }
    return static_cast<int>(code);
}

// The asset's bytes as text. The drive's error codes pass through; a short read means the asset changed under it.
inline int ecs_scene_asset_text(rv_cd *cd, const char *asset, std::string &text)
{
    const int64_t handle = rv_cd_asset_open(cd, asset);
    if (handle < 0) {
        RV_LOG_ERR(ecs_scene_records_tag, "scene '{}': open failed ({})", rv_pdklib::rv_log_escape(asset), handle);
        return ecs_scene_drive_code(handle);
    }
    const int64_t size = rv_cd_asset_size(cd, handle);
    if (size < 0) {
        RV_LOG_ERR(ecs_scene_records_tag, "scene '{}': size failed ({})", rv_pdklib::rv_log_escape(asset), size);
        return ecs_scene_drive_code(size);
    }
    text.resize(static_cast<std::size_t>(size));
    const int64_t got = rv_cd_asset_read(cd, handle, text.data(), size);
    if (got < 0) {
        RV_LOG_ERR(ecs_scene_records_tag, "scene '{}': read failed ({})", rv_pdklib::rv_log_escape(asset), got);
        return ecs_scene_drive_code(got);
    }
    if (got != size) {
        RV_LOG_ERR(ecs_scene_records_tag, "scene '{}': read {} of {} bytes", rv_pdklib::rv_log_escape(asset), got, size);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// The text fields of `object` into `out`. A name that does not fit or holds a NUL refuses the scene.
inline int ecs_scene_object_text(const std::string &scene, const rv_scene_object &object, ecs_scene_object &out)
{
    const std::string model = ecs_scene_model_name(object.mesh);
    const struct {
        const char *label;
        const std::string *value;
        char *dst;
    } fields[] = {
        { "id", &object.id, out.id },
        { "name", &object.name, out.name },
        { "model", &model, out.model },
        { "texture", &object.texture, out.texture },
    };
    for (const auto &field : fields) {
        const int copied = ecs_scene_copy_text(*field.value, field.dst, ecs_name_capacity);
        if (copied != RV_OK) {
            RV_LOG_ERR(ecs_scene_records_tag,
                "scene '{}' refused: object '{}' {} is longer than {} bytes or with a NUL",
                rv_pdklib::rv_log_escape(scene.c_str()),
                rv_pdklib::rv_log_escape(object.id.c_str()),
                field.label,
                ecs_name_capacity - 1);
            return copied;
        }
    }
    return RV_OK;
}

// Object `index` of `loaded` into `out`: names, kind, parent, world, uv, tint and tess. Parent and world are
// as the document resolves them; uv and tess convert from Q20.12 to the contract's float.
inline int ecs_scene_object_fill(const std::string &scene, const rv_scene &loaded, std::size_t index, ecs_scene_object &out)
{
    const rv_scene_object &object = loaded.objects[index];
    const int text = ecs_scene_object_text(scene, object, out);
    if (text != RV_OK) {
        return text;
    }
    if (!ecs_scene_kind_of(object.kind, out.kind)) {
        RV_LOG_ERR(ecs_scene_records_tag,
            "scene '{}' refused: object '{}' has kind '{}'",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(object.id.c_str()),
            rv_pdklib::rv_log_escape(object.kind.c_str()));
        return RV_ERR_INVAL;
    }
    out.parent = object.parent;
    // rv_mat4 is row-major float[4][4]: the same 16 floats as ecs_scene_object::world.
    const rv_pdklib::rv_mat4 world = rv_scene_world(loaded, index);
    static_assert(sizeof(out.world) == sizeof(world.m), "scene world is 16 floats");
    std::memcpy(out.world, world.m, sizeof(out.world));
    static_assert(std::size(out.uv) == std::size(object.uv), "scene uv element count");
    for (std::size_t i = 0; i < std::size(out.uv); ++i) {
        out.uv[i] = rv_pdklib::fx_to_float(object.uv[i]);
    }
    out.tint = object.tint;
    out.tess = rv_pdklib::fx_to_float(object.tess);
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

// The local transform of `object` as its Q20.12 raw values: the document's numbers, no conversion.
inline ecs_scene_local ecs_scene_local_of(const rv_scene_object &object)
{
    return ecs_scene_local{
        { object.position.x, object.position.y, object.position.z },
        { object.rotation.x, object.rotation.y, object.rotation.z },
        { object.scale.x, object.scale.y, object.scale.z },
    };
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

// Flattens `loaded` into `out`: one object, entity and local per object, then the components.
inline int ecs_scene_flatten(const std::string &scene, const rv_scene &loaded, ecs_scene_records &out)
{
    for (std::size_t i = 0; i < loaded.objects.size(); ++i) {
        ecs_scene_object object{};
        const int filled = ecs_scene_object_fill(scene, loaded, i, object);
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
        out.locals.push_back(ecs_scene_local_of(loaded.objects[i]));
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

// Best effort: the code is the result. Logging may need the memory that just ran out, and then the line is lost.
inline int ecs_scene_no_memory(const char *asset)
{
    try {
        RV_LOG_ERR(ecs_scene_records_tag, "scene '{}' refused: out of memory", rv_pdklib::rv_log_escape(asset));
    } catch (const std::bad_alloc &) {
        // the line is lost; the caller still returns RV_ERR_NOMEM
    }
    return RV_ERR_NOMEM;
}

// Reads scene document `asset` of `cd` into `out`, which is written only on success. Every refusal is logged
// with its reason: the drive's error for a failed read, RV_ERR_INVAL for a document the records cannot hold,
// RV_ERR_NOMEM for a failed allocation.
[[nodiscard]] inline int ecs_scene_records_read(rv_cd *cd, const char *asset, ecs_scene_records &out)
{
    try {
        std::string text;
        const int read = ecs_scene_asset_text(cd, asset, text);
        if (read != RV_OK) {
            return read;
        }
        rv_scene loaded;
        std::string error;
        if (rv_scene_parse(text, asset, loaded, error) != 0) {
            RV_LOG_ERR(ecs_scene_records_tag,
                "scene '{}' refused: {}",
                rv_pdklib::rv_log_escape(asset),
                rv_pdklib::rv_log_escape(error.c_str(), error.size()));
            return RV_ERR_INVAL;
        }
        ecs_scene_records built;
        const int flat = ecs_scene_flatten(asset, loaded, built);
        if (flat != RV_OK) {
            return flat;
        }
        out = std::move(built);
    } catch (const std::bad_alloc &) {
        return ecs_scene_no_memory(asset);
    } catch (const std::length_error &) {
        // text.resize with a size the string cannot hold
        return ecs_scene_no_memory(asset);
    }
    return RV_OK;
}

} // namespace rv_pdkkit
