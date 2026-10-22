#pragma once

// Loads a scene of the disc into the ECS world. The records come from the scene asset's bytes (rv_ecs_scene_records.hpp),
// not from the console. A failed load destroys its own entities and components: live ones are as before the call, while
// slot numbers and generations may differ.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
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
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace rv_pdkkit
{

inline constexpr const char *ecs_scene_tag = "ecs-scene";

// Factories the loader knows itself. They take precedence over a registered factory of the same name.
struct ecs_scene_builtin {
    std::string_view name;
    ecs_component_factory make;
};

inline constexpr ecs_scene_builtin ecs_scene_builtins[] = {
    { "hidden", ecs_hidden_factory },
    { "decal", ecs_decal_factory },
    { "two_sided", ecs_two_sided_factory },
};

// State of one load. made holds one entity per object, in file order.
struct ecs_scene_reading {
    ecs_world &world;
    const char *scene;
    const ecs_scene_records &records;
    int64_t objects;
    int64_t components;
    std::vector<ecs_id> &made;
};

// Boundary: a float of the scene contract to fx (uv in pixels, tess as subdivision density, ecs_scene_object in
// rv_ecs_scene_records.hpp). The only float entry.
[[nodiscard]] inline int ecs_scene_unit_from_float(float value, rv_pdklib::fx &out)
{
    return rv_pdklib::fx_from_double(static_cast<double>(value), out);
}

// Boundary: a float array of the PDK contract to fx. On error out may be partly written; the load then rolls back.
template <std::size_t N>
int ecs_scene_units_from_float(const float (&in)[N], rv_pdklib::fx (&out)[N])
{
    for (std::size_t i = 0; i < N; ++i) {
        const int converted = ecs_scene_unit_from_float(in[i], out[i]);
        if (converted != RV_OK) {
            return converted;
        }
    }
    return RV_OK;
}

// Name from a NUL-terminated scene array.
template <std::size_t N>
std::string ecs_scene_text(const char (&text)[N])
{
    return std::string(text, ecs_name_length(text));
}

// Copies a scene name into a component's array, zeroing the rest so the same name gives the same bytes. The static_assert
// keeps every source name whole (N is at least M, the source's capacity). A source with no NUL that fills its array is
// refused, not cut: RV_ERR_INVAL, logged, out untouched.
template <std::size_t N, std::size_t M>
int ecs_scene_copy_name(const char *scene, const char (&text)[M], char (&out)[N])
{
    static_assert(N >= M, "a component's name array must hold a whole scene name: its size is at least the source's size");
    const std::size_t length = ecs_name_length(text);
    if (length >= N) {
        RV_LOG_ERR(ecs_scene_tag, "scene '{}': name has no NUL within {} bytes", rv_pdklib::rv_log_escape(scene), M);
        return RV_ERR_INVAL;
    }
    std::memset(out, 0, N);
    std::memcpy(out, text, length);
    return RV_OK;
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

// Marker component T: no data, kept when already present.
template <typename T>
int ecs_scene_mark(ecs_scene_reading &load, ecs_id id)
{
    T *marker = nullptr;
    const int ensured = ecs_component_ensure<T>(load.world, id, marker);
    if (ensured != RV_OK) {
        return ecs_scene_ensure_failed(load, ensured);
    }
    return RV_OK;
}

// A mesh's model with its tint. A mesh without a model gets none, with a warning.
inline int ecs_scene_add_model(ecs_scene_reading &load, const ecs_scene_object &object, ecs_id id)
{
    const bool named = object.model[0] != '\0';
    if (!named) {
        RV_LOG_WARN(ecs_scene_tag,
            "scene '{}' object '{}': mesh has no model",
            rv_pdklib::rv_log_escape(load.scene),
            ecs_scene_text(object.name));
        return RV_OK;
    }
    ecs_component_model *model = nullptr;
    const int ensured = ecs_component_ensure<ecs_component_model>(load.world, id, model);
    if (ensured != RV_OK) {
        return ecs_scene_ensure_failed(load, ensured);
    }
    const int copied = ecs_scene_copy_name(load.scene, object.model, model->name);
    if (copied != RV_OK) {
        return copied;
    }
    model->tint = object.tint;
    return RV_OK;
}

// Sprite of a quad or billboard. uv (pixels) and tess (subdivision density) convert at the boundary.
inline int ecs_scene_add_sprite(ecs_scene_reading &load, const ecs_scene_object &object, ecs_id id, ecs_sprite_kind kind)
{
    ecs_component_sprite *sprite = nullptr;
    const int ensured = ecs_component_ensure<ecs_component_sprite>(load.world, id, sprite);
    if (ensured != RV_OK) {
        return ecs_scene_ensure_failed(load, ensured);
    }
    const int uv_status = ecs_scene_units_from_float(object.uv, sprite->uv);
    if (uv_status != RV_OK) {
        RV_LOG_ERR(ecs_scene_tag,
            "scene '{}' object '{}': uv does not fit ({})",
            rv_pdklib::rv_log_escape(load.scene),
            ecs_scene_text(object.name),
            uv_status);
        return uv_status;
    }
    const int tess_status = ecs_scene_unit_from_float(object.tess, sprite->tess);
    if (tess_status != RV_OK) {
        RV_LOG_ERR(ecs_scene_tag,
            "scene '{}' object '{}': tess does not fit ({})",
            rv_pdklib::rv_log_escape(load.scene),
            ecs_scene_text(object.name),
            tess_status);
        return tess_status;
    }
    sprite->kind = kind;
    sprite->texture = ecs_scene_text(object.texture);
    sprite->tint = object.tint;
    return RV_OK;
}

// Components of an object's kind. Groups have none.
inline int ecs_scene_add_kind(ecs_scene_reading &load, const ecs_scene_object &object, ecs_id id)
{
    switch (object.kind) {
    case ecs_scene_kind::group:
        return RV_OK;
    case ecs_scene_kind::camera:
        return ecs_scene_mark<ecs_component_camera>(load, id);
    case ecs_scene_kind::mesh:
        return ecs_scene_add_model(load, object, id);
    case ecs_scene_kind::quad:
        return ecs_scene_add_sprite(load, object, id, ecs_sprite_kind::quad);
    case ecs_scene_kind::billboard:
        return ecs_scene_add_sprite(load, object, id, ecs_sprite_kind::billboard);
    case ecs_scene_kind::volume:
        return ecs_scene_mark<ecs_component_volume>(load, id);
    }
    RV_LOG_ERR(ecs_scene_tag,
        "scene '{}' object '{}': unknown kind {}",
        rv_pdklib::rv_log_escape(load.scene),
        ecs_scene_text(object.name),
        static_cast<int>(object.kind));
    return RV_ERR_INVAL;
}

// Transform from the local Q20.12 values. The hierarchy is set later by ecs_scene_link, once every object has an entity.
inline int ecs_scene_add_transform(ecs_scene_reading &load, int64_t index, ecs_id id)
{
    const ecs_scene_local &local = load.records.locals[static_cast<std::size_t>(index)];
    ecs_component_transform *transform = nullptr;
    const int ensured = ecs_component_ensure<ecs_component_transform>(load.world, id, transform);
    if (ensured != RV_OK) {
        return ecs_scene_ensure_failed(load, ensured);
    }
    // Q20.12 int32 is fx: the values are copied as they are.
    transform->position = rv_pdklib::fx_vec3{ local.position[0], local.position[1], local.position[2] };
    transform->rotation = rv_pdklib::fx_vec3{ local.rotation[0], local.rotation[1], local.rotation[2] };
    transform->scale = rv_pdklib::fx_vec3{ local.scale[0], local.scale[1], local.scale[2] };
    return RV_OK;
}

// Creates the entity of object `index` with its type, transform, name and kind components.
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
            ecs_scene_text(object.name),
            rv_pdklib::rv_log_escape(type.type),
            created);
        return created;
    }
    load.made.push_back(id); // reserved in advance: no allocation
    const int placed = ecs_scene_add_transform(load, index, id);
    if (placed != RV_OK) {
        return placed;
    }
    ecs_component_name *name = nullptr;
    const int named = ecs_component_ensure<ecs_component_name>(load.world, id, name);
    if (named != RV_OK) {
        return ecs_scene_ensure_failed(load, named);
    }
    const int copied = ecs_scene_copy_name(load.scene, object.name, name->text);
    if (copied != RV_OK) {
        return copied;
    }
    return ecs_scene_add_kind(load, object, id);
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
            ecs_scene_text(object.name),
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

// Factory of a name: a built-in first, then the world's registry. nullptr when neither has it.
inline ecs_component_factory ecs_scene_factory(const ecs_world &world, std::string_view name)
{
    for (const ecs_scene_builtin &builtin : ecs_scene_builtins) {
        if (builtin.name == name) {
            return builtin.make;
        }
    }
    return ecs_component_find(world, name);
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
    const ecs_component_factory make = ecs_scene_factory(load.world, name);
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
