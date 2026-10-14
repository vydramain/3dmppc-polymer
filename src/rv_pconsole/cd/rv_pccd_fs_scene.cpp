// rv_pccd_fs SCENE residency: a .scene.toml read from the disc, parsed by pdklib
// and flattened into rv_cd_scene_object copies the disc reads by index. Kept out
// of rv_pccd_fs.cpp, which is at its size limit.
#include <cstring>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_scene/rv_scene.hpp"
#include "rv_pconsole/cd/rv_pccd_fs.hpp"

namespace
{
// An authored mesh "x.glb" or "x.gltf" is served from the disc as "x.mppcmod".
constexpr std::string_view RV_PCCD_FS_SCENE_MESH_EXT_GLB = ".glb";
constexpr std::string_view RV_PCCD_FS_SCENE_MESH_EXT_GLTF = ".gltf";
constexpr std::string_view RV_PCCD_FS_SCENE_MESH_EXTS[] = { RV_PCCD_FS_SCENE_MESH_EXT_GLB, RV_PCCD_FS_SCENE_MESH_EXT_GLTF };
constexpr std::string_view RV_PCCD_FS_SCENE_MODEL_EXT = ".mppcmod";

// The kind words pdklib reads, and the rv_cd_scene_kind each one is.
struct scene_kind_entry {
    std::string_view word;
    rv_cd_scene_kind kind;
};
constexpr scene_kind_entry RV_PCCD_FS_SCENE_KINDS[] = {
    { "group", RV_CD_SCENE_GROUP },
    { "camera", RV_CD_SCENE_CAMERA },
    { "mesh", RV_CD_SCENE_MESH },
    { "quad", RV_CD_SCENE_QUAD },
    { "billboard", RV_CD_SCENE_BILLBOARD },
    { "volume", RV_CD_SCENE_VOLUME },
};

// True, with `kind_out` set, when `word` is a kind; false otherwise.
bool scene_kind_of_(std::string_view word, rv_cd_scene_kind &kind_out)
{
    for (const scene_kind_entry &entry : RV_PCCD_FS_SCENE_KINDS) {
        if (entry.word == word) {
            kind_out = entry.kind;
            return true;
        }
    }
    return false;
}

// The disc name of a scene mesh. An empty mesh stays empty; a name that ends in
// neither .glb nor .gltf is already a disc name and passes unchanged.
std::string scene_model_name_(const std::string &mesh)
{
    for (const std::string_view ext : RV_PCCD_FS_SCENE_MESH_EXTS) {
        if (std::string_view(mesh).ends_with(ext)) {
            return mesh.substr(0, mesh.size() - ext.size()) + std::string(RV_PCCD_FS_SCENE_MODEL_EXT);
        }
    }
    return mesh;
}

// Copies `value` into `dst` of `capacity` bytes, NUL included. False when it does not fit,
// or holds a NUL byte: a C reader would see the text cut short.
bool copy_text_(const std::string &value, char *dst, size_t capacity)
{
    if (value.size() >= capacity || value.find('\0') != std::string::npos) {
        return false;
    }
    std::memcpy(dst, value.data(), value.size());
    dst[value.size()] = '\0';
    return true;
}

// Flattens object `index` of `loaded` into `out`. Any name that does not fit
// or holds a NUL refuses the whole scene, with the field and object named in the log.
int64_t build_object_(const std::string &scene, const rv_pdklib::rv_scene &loaded, size_t index, rv_cd_scene_object &out)
{
    const rv_pdklib::rv_scene_object &object = loaded.objects[index];
    const std::string model = scene_model_name_(object.mesh);
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
        if (!copy_text_(*field.value, field.dst, RV_CD_NAME_CAPACITY)) {
            RV_LOG_WARN("pccd",
                "scene '{}' refused: object '{}' {} is longer than {} bytes or with a NUL",
                rv_pdklib::rv_log_escape(scene.c_str()),
                rv_pdklib::rv_log_escape(object.id.c_str()),
                field.label,
                RV_CD_NAME_CAPACITY - 1);
            return RV_ERR_INVAL;
        }
    }

    if (!scene_kind_of_(object.kind, out.kind)) {
        RV_LOG_WARN("pccd",
            "scene '{}' refused: object '{}' has kind '{}'",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(object.id.c_str()),
            rv_pdklib::rv_log_escape(object.kind.c_str()));
        return RV_ERR_INVAL;
    }

    out.parent = object.parent;
    // rv_mat4 is row-major float[4][4]: the same 16 floats as rv_cd_scene_object::world.
    const rv_pdklib::rv_mat4 world = rv_pdklib::rv_scene_world(loaded, index);
    static_assert(sizeof(out.world) == sizeof(world.m), "scene world is 16 floats");
    std::memcpy(out.world, world.m, sizeof(out.world));
    // The document keeps uv and tess as Q20.12 fixed point; the console contract is float.
    static_assert(sizeof(out.uv) / sizeof(out.uv[0]) == sizeof(object.uv) / sizeof(object.uv[0]), "scene uv element count");
    for (size_t i = 0; i < std::size(out.uv); ++i) {
        out.uv[i] = rv_pdklib::fx_to_float(object.uv[i]);
    }
    out.tint = object.tint;
    out.tess = rv_pdklib::fx_to_float(object.tess);
    return RV_OK;
}

// The entity type of `object` into `out`; a type that does not fit or holds a NUL
// refuses the scene.
int64_t build_entity_(const std::string &scene, const rv_pdklib::rv_scene_object &object, rv_cd_scene_entity &out)
{
    if (!copy_text_(object.entity, out.type, RV_CD_NAME_CAPACITY)) {
        RV_LOG_WARN("pccd",
            "scene '{}' refused: object '{}' entity type is longer than {} bytes or with a NUL",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(object.id.c_str()),
            RV_CD_NAME_CAPACITY - 1);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// The local transform of `object` as its Q20.12 raw values: the document's numbers, no conversion.
rv_cd_scene_local local_of_(const rv_pdklib::rv_scene_object &object)
{
    return rv_cd_scene_local{
        { object.position.x, object.position.y, object.position.z },
        { object.rotation.x, object.rotation.y, object.rotation.z },
        { object.scale.x, object.scale.y, object.scale.z },
    };
}

// Flattens `component` into `out`: its object, name and fields as text. Too many fields,
// or a name, key or value that does not fit or holds a NUL, refuses the scene.
int64_t build_component_(const std::string &scene,
    const rv_pdklib::rv_scene &loaded,
    const rv_pdklib::rv_scene_component &component,
    rv_cd_scene_component &out)
{
    const int64_t field_count = static_cast<int64_t>(component.fields.size());
    if (field_count > RV_CD_COMPONENT_FIELD_CAPACITY) {
        RV_LOG_WARN("pccd",
            "scene '{}' refused: component '{}' has {} fields, the limit is {}",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(component.name.c_str()),
            field_count,
            RV_CD_COMPONENT_FIELD_CAPACITY);
        return RV_ERR_INVAL;
    }
    if (!copy_text_(component.name, out.name, RV_CD_NAME_CAPACITY)) {
        // The parser resolves component.object to an index of loaded.objects.
        RV_LOG_WARN("pccd",
            "scene '{}' refused: component of object '{}' has a name longer than {} bytes or with a NUL",
            rv_pdklib::rv_log_escape(scene.c_str()),
            rv_pdklib::rv_log_escape(loaded.objects[static_cast<size_t>(component.object)].id.c_str()),
            RV_CD_NAME_CAPACITY - 1);
        return RV_ERR_INVAL;
    }
    out.object = component.object;
    out.field_count = field_count;
    for (size_t i = 0; i < component.fields.size(); ++i) {
        const rv_pdklib::rv_manifest_tree_entry &entry = component.fields[i];
        if (!copy_text_(entry.key, out.fields[i].key, RV_CD_NAME_CAPACITY)) {
            RV_LOG_WARN("pccd",
                "scene '{}' refused: component '{}' has a field key longer than {} bytes or with a NUL",
                rv_pdklib::rv_log_escape(scene.c_str()),
                rv_pdklib::rv_log_escape(component.name.c_str()),
                RV_CD_NAME_CAPACITY - 1);
            return RV_ERR_INVAL;
        }
        const std::string value = rv_pdklib::rv_scene_value_text(entry.value);
        if (!copy_text_(value, out.fields[i].value, RV_CD_VALUE_CAPACITY)) {
            RV_LOG_WARN("pccd",
                "scene '{}' refused: component '{}' field '{}' value is longer than {} bytes or with a NUL",
                rv_pdklib::rv_log_escape(scene.c_str()),
                rv_pdklib::rv_log_escape(component.name.c_str()),
                rv_pdklib::rv_log_escape(entry.key.c_str()),
                RV_CD_VALUE_CAPACITY - 1);
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

} // namespace

namespace rv_3dmppc
{

int64_t rv_pccd_fs::scene_object_count(const char *scene)
{
    scene_record *record = nullptr;
    const int64_t rc = scene_resolve_(scene, record);
    if (rc < 0) {
        return rc;
    }
    return static_cast<int64_t>(record->objects.size());
}

int64_t rv_pccd_fs::scene_object_read(const char *scene, int64_t index, rv_cd_scene_object *out)
{
    if (out == nullptr) {
        RV_LOG_WARN("pccd", "scene_object_read with a null out");
        return RV_ERR_INVAL;
    }
    scene_record *record = nullptr;
    const int64_t rc = scene_resolve_(scene, record);
    if (rc < 0) {
        return rc;
    }
    const int64_t count = static_cast<int64_t>(record->objects.size());
    if (index < 0 || index >= count) {
        RV_LOG_WARN("pccd",
            "scene '{}' has {} objects, index {} is out of range",
            rv_pdklib::rv_log_escape(scene),
            count,
            index);
        return RV_ERR_INVAL;
    }
    *out = record->objects[static_cast<size_t>(index)];
    return RV_OK;
}

int64_t rv_pccd_fs::scene_component_count(const char *scene)
{
    scene_record *record = nullptr;
    const int64_t rc = scene_resolve_(scene, record);
    if (rc < 0) {
        return rc;
    }
    return static_cast<int64_t>(record->components.size());
}

int64_t rv_pccd_fs::scene_component_read(const char *scene, int64_t index, rv_cd_scene_component *out)
{
    if (out == nullptr) {
        RV_LOG_WARN("pccd", "scene_component_read with a null out");
        return RV_ERR_INVAL;
    }
    scene_record *record = nullptr;
    const int64_t rc = scene_resolve_(scene, record);
    if (rc < 0) {
        return rc;
    }
    const int64_t count = static_cast<int64_t>(record->components.size());
    if (index < 0 || index >= count) {
        RV_LOG_WARN("pccd",
            "scene '{}' has {} components, index {} is out of range",
            rv_pdklib::rv_log_escape(scene),
            count,
            index);
        return RV_ERR_INVAL;
    }
    *out = record->components[static_cast<size_t>(index)];
    return RV_OK;
}

int64_t rv_pccd_fs::scene_object_entity_read(const char *scene, int64_t index, rv_cd_scene_entity *out)
{
    if (out == nullptr) {
        RV_LOG_WARN("pccd", "scene_object_entity_read with a null out");
        return RV_ERR_INVAL;
    }
    scene_record *record = nullptr;
    const int64_t rc = scene_resolve_(scene, record);
    if (rc < 0) {
        return rc;
    }
    const int64_t count = static_cast<int64_t>(record->entities.size());
    if (index < 0 || index >= count) {
        RV_LOG_WARN("pccd",
            "scene '{}' has {} objects, index {} is out of range",
            rv_pdklib::rv_log_escape(scene),
            count,
            index);
        return RV_ERR_INVAL;
    }
    *out = record->entities[static_cast<size_t>(index)];
    return RV_OK;
}

int64_t rv_pccd_fs::scene_object_local_read(const char *scene, int64_t index, rv_cd_scene_local *out)
{
    if (out == nullptr) {
        RV_LOG_WARN("pccd", "scene_object_local_read with a null out");
        return RV_ERR_INVAL;
    }
    scene_record *record = nullptr;
    const int64_t rc = scene_resolve_(scene, record);
    if (rc < 0) {
        return rc;
    }
    const int64_t count = static_cast<int64_t>(record->locals.size());
    if (index < 0 || index >= count) {
        RV_LOG_WARN("pccd",
            "scene '{}' has {} objects, index {} is out of range",
            rv_pdklib::rv_log_escape(scene),
            count,
            index);
        return RV_ERR_INVAL;
    }
    *out = record->locals[static_cast<size_t>(index)];
    return RV_OK;
}

int64_t rv_pccd_fs::scene_resolve_(const char *scene, scene_record *&record_out)
{
    if (scene == nullptr) {
        RV_LOG_WARN("pccd", "scene query with a null name");
        return RV_ERR_INVAL;
    }
    // Checked before the cache: a name this long can never have been loaded.
    if (std::strlen(scene) >= RV_CD_NAME_CAPACITY) {
        RV_LOG_WARN("pccd",
            "scene '{}' refused: its name is longer than {} bytes",
            rv_pdklib::rv_log_escape(scene),
            RV_CD_NAME_CAPACITY - 1);
        return RV_ERR_INVAL;
    }

    // The key copy and the table growth are the allocations: bad_alloc becomes
    // RV_ERR_NOMEM here, never an exception across the extern "C" boundary.
    int64_t index = -1;
    try {
        const std::string key(scene);
        const auto it = scene_by_name_.find(key);
        if (it != scene_by_name_.end()) {
            record_out = &scenes_[static_cast<size_t>(it->second)];
            return RV_OK;
        }

        scene_record record;
        const int64_t rc = scene_load_(scene, record);
        if (rc < 0) {
            return rc;
        }

        index = static_cast<int64_t>(scenes_.size());
        scenes_.push_back(std::move(record));
        scene_by_name_.emplace(key, index);
    } catch (const std::bad_alloc &) {
        if (index >= 0 && static_cast<int64_t>(scenes_.size()) > index) {
            scenes_.pop_back();
        }
        return RV_ERR_NOMEM;
    }

    record_out = &scenes_[static_cast<size_t>(index)];
    return RV_OK;
}

int64_t rv_pccd_fs::scene_load_(const char *scene, scene_record &record)
{
    // Every allocation in the load (the byte and text copies, the parser, the
    // object vector) answers RV_ERR_NOMEM; none may escape to the C caller.
    try {
        std::vector<std::byte> bytes;
        const int64_t read_rc = asset_read_bytes_(scene, bytes);
        if (read_rc < 0) {
            return read_rc;
        }

        const std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        rv_pdklib::rv_scene loaded;
        std::string error;
        if (rv_pdklib::rv_scene_parse(text, scene, loaded, error) != 0) {
            RV_LOG_WARN("pccd", "scene '{}' refused: {}", rv_pdklib::rv_log_escape(scene), error);
            return RV_ERR_INVAL;
        }

        for (size_t i = 0; i < loaded.objects.size(); ++i) {
            rv_cd_scene_object object{};
            const int64_t rc = build_object_(scene, loaded, i, object);
            if (rc < 0) {
                return rc;
            }
            rv_cd_scene_entity entity{};
            const int64_t entity_rc = build_entity_(scene, loaded.objects[i], entity);
            if (entity_rc < 0) {
                return entity_rc;
            }
            record.objects.push_back(object);
            record.entities.push_back(entity);
            record.locals.push_back(local_of_(loaded.objects[i]));
        }
        for (const rv_pdklib::rv_scene_component &component : loaded.components) {
            rv_cd_scene_component flat{};
            const int64_t component_rc = build_component_(scene, loaded, component, flat);
            if (component_rc < 0) {
                return component_rc;
            }
            record.components.push_back(flat);
        }
    } catch (const std::bad_alloc &) {
        return RV_ERR_NOMEM;
    }
    return RV_OK;
}

} // namespace rv_3dmppc
