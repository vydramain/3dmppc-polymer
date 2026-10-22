#pragma once

// Loads a scene of the disc into the ECS world. The scene asset is read here (ecs_scene_records_read), migrated to form 2
// in memory, and its records (rv_ecs_scene_records.hpp) become entities and components. A failed load destroys its own
// entities and components: live ones are as before the call, while slot numbers and generations may differ.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_scene_records.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdkkit/scene/rv_scene.hpp"
#include "pdkkit/scene/rv_scene_migrate.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_pdkkit
{

inline constexpr const char *ecs_scene_tag = "ecs-scene";

// State of one load. made holds one entity per object, in file order.
struct ecs_scene_reading {
    ecs_world &world;
    const char *scene;
    const ecs_scene_records &records;
    int64_t objects;
    int64_t components;
    std::vector<ecs_id> &made;
};

// Name from a NUL-terminated scene array.
template <std::size_t N>
std::string ecs_scene_text(const char (&text)[N])
{
    return std::string(text, ecs_name_length(text));
}

// Logged once: an entity the load has just made is not alive.
inline int ecs_scene_no_entity(const char *scene)
{
    RV_LOG_ERR(ecs_scene_tag, "scene '{}': entity is not alive", rv_pdklib::rv_log_escape(scene));
    return RV_ERR_NOENT;
}

// Failure of ecs_component_ensure: a stale entity is logged as before, any other code (RV_ERR_NOMEM) is returned as is.
inline int ecs_scene_ensure_failed(const ecs_scene_reading &load, int code)
{
    if (code == RV_ERR_NOENT) {
        return ecs_scene_no_entity(load.scene);
    }
    RV_LOG_ERR(ecs_scene_tag, "scene '{}': component add failed ({})", rv_pdklib::rv_log_escape(load.scene), code);
    return code;
}

// Capacity for the whole load and its rollback, taken before the first change, so the rollback never allocates.
inline void ecs_scene_reserve(ecs_scene_reading &load)
{
    auto &state = detail::ecs_state(load.world);
    const std::size_t objects = static_cast<std::size_t>(load.objects);
    state.slots.reserve(state.slots.size() + objects);
    state.free_slots.reserve(state.slots.size() + objects);
    load.made.reserve(objects);
}

// Object `index` of the records; read_into has checked that the index is in range.
inline const ecs_scene_object &ecs_scene_object_at(const ecs_scene_reading &load, int64_t index)
{
    return load.records.objects[static_cast<std::size_t>(index)];
}

// Creates the entity of object `index` with its type. Its components come from their factories, in ecs_scene_add_component.
inline int ecs_scene_spawn(ecs_scene_reading &load, int64_t index)
{
    const ecs_scene_object &object = ecs_scene_object_at(load, index);
    const ecs_scene_entity &type = load.records.entities[static_cast<std::size_t>(index)];
    ecs_id id{};
    const std::string_view type_name{ type.type, ecs_name_length(type.type) };
    const int created = ecs_entity_create_as(load.world, type_name, id);
    if (created != RV_OK) {
        RV_LOG_ERR(ecs_scene_tag,
            "scene '{}' object '{}': entity type '{}' failed ({})",
            rv_pdklib::rv_log_escape(load.scene),
            ecs_scene_text(object.id),
            rv_pdklib::rv_log_escape(type.type),
            created);
        return created;
    }
    load.made.push_back(id); // reserved in advance: no allocation
    return RV_OK;
}

// Parent of object `index`: the entity of its parent object, or `parent` for a root. ecs_component_parent is the
// hierarchy; a world root has none, so the component is added only when there is a parent entity.
inline int ecs_scene_link(ecs_scene_reading &load, int64_t index, ecs_id parent)
{
    const ecs_scene_object &object = ecs_scene_object_at(load, index);
    const bool outside = object.parent >= load.objects;
    if (outside) {
        RV_LOG_ERR(ecs_scene_tag,
            "scene '{}' object '{}': parent index {} is out of range",
            rv_pdklib::rv_log_escape(load.scene),
            ecs_scene_text(object.id),
            object.parent);
        return RV_ERR_INVAL;
    }
    const bool root = object.parent < 0;
    const ecs_id entity_parent = root ? parent : load.made[static_cast<std::size_t>(object.parent)];
    const bool has_parent = entity_parent != ecs_id{};
    if (!has_parent) {
        return RV_OK;
    }
    const ecs_id id = load.made[static_cast<std::size_t>(index)];
    ecs_component_parent *link = nullptr;
    const int ensured = ecs_component_ensure<ecs_component_parent>(load.world, id, link);
    if (ensured != RV_OK) {
        return ecs_scene_ensure_failed(load, ensured);
    }
    link->parent = entity_parent;
    return RV_OK;
}

// One [[component]] of the scene, given to its object's factory. An unknown name is skipped with a warning.
inline int ecs_scene_add_component(ecs_scene_reading &load, int64_t index)
{
    const ecs_scene_component &component = load.records.components[static_cast<std::size_t>(index)];
    const bool outside = component.object < 0 || component.object >= load.objects;
    if (outside) {
        RV_LOG_ERR(ecs_scene_tag,
            "scene '{}' component '{}': object index {} is out of range",
            rv_pdklib::rv_log_escape(load.scene),
            rv_pdklib::rv_log_escape(component.name),
            component.object);
        return RV_ERR_INVAL;
    }
    const std::string_view name{ component.name, ecs_name_length(component.name) };
    const ecs_component_factory make = ecs_component_find(load.world, name);
    if (make == nullptr) {
        RV_LOG_WARN(ecs_scene_tag,
            "scene '{}' object {}: unknown component '{}' skipped",
            rv_pdklib::rv_log_escape(load.scene),
            component.object,
            rv_pdklib::rv_log_escape(component.name));
        return RV_OK;
    }
    const ecs_id id = load.made[static_cast<std::size_t>(component.object)];
    const int filled = make(load.world, id, component);
    if (filled != RV_OK) {
        RV_LOG_ERR(ecs_scene_tag,
            "scene '{}' object {}: component '{}' failed ({})",
            rv_pdklib::rv_log_escape(load.scene),
            component.object,
            rv_pdklib::rv_log_escape(component.name),
            filled);
        return filled;
    }
    return RV_OK;
}

// Objects in file order, then parents, then components. Every step returns its failure as is.
inline int ecs_scene_load_all(ecs_scene_reading &load, ecs_id parent)
{
    ecs_scene_reserve(load);
    for (int64_t index = 0; index < load.objects; ++index) {
        const int spawned = ecs_scene_spawn(load, index);
        if (spawned != RV_OK) {
            return spawned;
        }
    }
    for (int64_t index = 0; index < load.objects; ++index) {
        const int linked = ecs_scene_link(load, index, parent);
        if (linked != RV_OK) {
            return linked;
        }
    }
    for (int64_t index = 0; index < load.components; ++index) {
        const int filled = ecs_scene_add_component(load, index);
        if (filled != RV_OK) {
            return filled;
        }
    }
    return RV_OK;
}

// Destroys the entities of a failed load. Each id is alive, and the slots were reserved, so nothing here allocates.
inline void ecs_scene_rollback(ecs_world &world, const std::vector<ecs_id> &made)
{
    for (const ecs_id id : made) {
        (void)ecs_entity_destroy(world, id);
    }
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
        RV_LOG_ERR(ecs_scene_tag, "scene '{}': open failed ({})", rv_pdklib::rv_log_escape(asset), handle);
        return ecs_scene_drive_code(handle);
    }
    const int64_t size = rv_cd_asset_size(cd, handle);
    if (size < 0) {
        RV_LOG_ERR(ecs_scene_tag, "scene '{}': size failed ({})", rv_pdklib::rv_log_escape(asset), size);
        return ecs_scene_drive_code(size);
    }
    text.resize(static_cast<std::size_t>(size));
    const int64_t got = rv_cd_asset_read(cd, handle, text.data(), size);
    if (got < 0) {
        RV_LOG_ERR(ecs_scene_tag, "scene '{}': read failed ({})", rv_pdklib::rv_log_escape(asset), got);
        return ecs_scene_drive_code(got);
    }
    if (got != size) {
        RV_LOG_ERR(ecs_scene_tag, "scene '{}': read {} of {} bytes", rv_pdklib::rv_log_escape(asset), got, size);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Best effort: the code is the result. Logging may need the memory that just ran out, and then the line is lost.
inline int ecs_scene_no_memory(const char *asset)
{
    try {
        RV_LOG_ERR(ecs_scene_tag, "scene '{}' refused: out of memory", rv_pdklib::rv_log_escape(asset));
    } catch (const std::bad_alloc &) {
        // the line is lost; the caller still returns RV_ERR_NOMEM
    }
    return RV_ERR_NOMEM;
}

// Logs migrate's warnings, one RV_LOG_WARN line each with the scene name. Each line ends with '\n' and is escaped whole.
// Best effort per line: a bad_alloc while logging loses that line, and the next lines and the load go on.
inline void ecs_scene_log_warnings(const char *asset, const std::string &warnings)
{
    std::size_t begin = 0;
    while (begin < warnings.size()) {
        std::size_t end = warnings.find('\n', begin);
        if (end == std::string::npos) {
            end = warnings.size();
        }
        try {
            const std::string line = warnings.substr(begin, end - begin);
            RV_LOG_WARN(ecs_scene_tag,
                "scene '{}': {}",
                rv_pdklib::rv_log_escape(asset),
                rv_pdklib::rv_log_escape(line.c_str(), line.size()));
        } catch (const std::bad_alloc &) {
            // the line is lost; the next line and the load go on
        }
        begin = end + 1;
    }
}

// Reads scene document `asset` of `cd` into `out` as form 2: parsed, migrated in memory, then flattened. out is written
// only on success. Every refusal is logged with its reason: the drive's error for a failed read, RV_ERR_INVAL for a
// document the records cannot hold, the migration's code for a refused migration, RV_ERR_NOMEM for a failed allocation.
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
            RV_LOG_ERR(ecs_scene_tag,
                "scene '{}' refused: {}",
                rv_pdklib::rv_log_escape(asset),
                rv_pdklib::rv_log_escape(error.c_str(), error.size()));
            return RV_ERR_INVAL;
        }
        std::string warnings;
        const int migrated = rv_scene_migrate(loaded, warnings);
        if (migrated != RV_OK) {
            RV_LOG_ERR(ecs_scene_tag,
                "scene '{}' refused: {}",
                rv_pdklib::rv_log_escape(asset),
                rv_pdklib::rv_log_escape(warnings.c_str(), warnings.size()));
            return migrated;
        }
        ecs_scene_log_warnings(asset, warnings);
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

// Parent is checked and the records are read before any entity exists; then the load runs into made.
inline int ecs_scene_read_into(ecs_world &world, rv_cd *cd, const char *scene, ecs_id parent, std::vector<ecs_id> &made)
{
    const bool root = parent == ecs_id{};
    const bool alive = root || ecs_entity_alive(world, parent);
    if (!alive) {
        RV_LOG_ERR(ecs_scene_tag, "scene '{}': parent entity is not alive", rv_pdklib::rv_log_escape(scene));
        return RV_ERR_NOENT;
    }
    ecs_scene_records records;
    const int read = ecs_scene_records_read(cd, scene, records);
    if (read != RV_OK) {
        return read;
    }
    const int64_t objects = static_cast<int64_t>(records.objects.size());
    const int64_t components = static_cast<int64_t>(records.components.size());
    ecs_scene_reading load{ world, scene, records, objects, components, made };
    return ecs_scene_load_all(load, parent);
}

// Best effort: the returned status is the result. Formatting may need the memory that just ran out, and then the line is lost.
inline void ecs_scene_log_failure(const char *scene, int status)
{
    try {
        RV_LOG_ERR(ecs_scene_tag, "scene '{}': not loaded, world restored ({})", rv_pdklib::rv_log_escape(scene), status);
    } catch (const std::bad_alloc &) {
        // the line is lost; the caller still returns status
    }
}

// Loads scene `scene` of cd into world. Objects become entities in file order in out; roots take `parent`
// (ecs_id{} is the world root, else it must be alive). On failure the load's entities are destroyed, out is
// empty, and the reason is logged. An exception anywhere in the call, a disc's factory included, rolls back the
// same way: bad_alloc gives RV_ERR_NOMEM, any other exception RV_ERR_INVAL, as in ecs_guard.
[[nodiscard]] inline int ecs_scene_read(ecs_world &world, rv_cd *cd, const char *scene, ecs_id parent, std::vector<ecs_id> &out)
{
    out.clear();
    std::vector<ecs_id> made;
    const int status = ecs_guard([&] {
        return ecs_scene_read_into(world, cd, scene, parent, made);
    });
    if (status != RV_OK) {
        ecs_scene_rollback(world, made);
        ecs_scene_log_failure(scene, status);
        return status;
    }
    out = std::move(made);
    return RV_OK;
}

} // namespace rv_pdkkit
