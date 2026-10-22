#pragma once

// Migrates a parsed scene from form 1 (an object with a kind and its data as fields) to form 2 (the data as
// [[component]] blocks), in memory. The ECS loader and the editor will call it; rv_scene_parse does not.

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/scene/rv_scene.hpp"
#include "pdkkit/scene/rv_scene_write.hpp"
#include "pdklib/rv_manifest/detail/rv_manifest_tree.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_math/rv_fx_text.hpp"

namespace rv_pdkkit
{

namespace detail
{

// Form-1 kinds that give components; "group" gives none.
inline constexpr const char *migrate_kind_mesh = "mesh";
inline constexpr const char *migrate_kind_camera = "camera";
inline constexpr const char *migrate_kind_volume = "volume";

// The sprites' undescribed components: not in the ECS table, so their names and keys are literal here.
inline constexpr const char *legacy_quad_name = "legacy_quad";
inline constexpr const char *legacy_billboard_name = "legacy_billboard";
inline constexpr const char *legacy_texture_key = "texture";
inline constexpr const char *legacy_uv_key = "uv";
inline constexpr const char *legacy_tint_key = "tint";
inline constexpr const char *legacy_tess_key = "tess";

// Text of the warnings; each line starts with the object's id.
inline constexpr const char *migrate_line_end = "\n";
inline constexpr const char *migrate_list_sep = ", ";

using migrate_fields = std::vector<rv_pdklib::rv_manifest_tree_entry>;

// Describes T by its ECS description: the name and the field at index, in declaration order.
template <typename T>
inline std::string migrate_name_of()
{
    return ecs_component_traits<T>::desc.name;
}

template <typename T>
inline std::string migrate_field_of(std::size_t index)
{
    return ecs_component_traits<T>::desc.fields[index].name;
}

inline void migrate_warn(std::string &warnings, const std::string &id, const std::string &text)
{
    warnings += "object '" + id + "': " + text + migrate_line_end;
}

inline bool migrate_is_sprite(const std::string &kind)
{
    return kind == kind_quad || kind == kind_billboard;
}

inline bool migrate_is_white(const rv_color &c)
{
    return c.r == ecs_tint_white.r && c.g == ecs_tint_white.g && c.b == ecs_tint_white.b;
}

inline bool migrate_same_vec3(const rv_pdklib::fx_vec3 &a, const rv_pdklib::fx_vec3 &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// A Q20.12 value. Its text is fx_to_text, which reads back to raw; the double is the value of that text.
// The text is always valid fx_to_text output, so from_chars cannot fail and its result is not checked.
inline rv_pdklib::rv_manifest_mvalue migrate_fx(rv_pdklib::fx raw)
{
    rv_pdklib::rv_manifest_mvalue v;
    v.kind = rv_pdklib::rv_manifest_value_kind::real;
    v.num_text = rv_pdklib::fx_to_text(raw);
    std::from_chars(v.num_text.data(), v.num_text.data() + v.num_text.size(), v.real);
    return v;
}

// Numbers of Q20.12 values, each with its text as migrate_fx builds it.
inline rv_pdklib::rv_manifest_mvalue migrate_fx_list(const rv_pdklib::fx *raw, std::size_t count)
{
    rv_pdklib::rv_manifest_mvalue v;
    v.kind = rv_pdklib::rv_manifest_value_kind::numbers;
    for (std::size_t i = 0; i < count; ++i) {
        const rv_pdklib::rv_manifest_mvalue item = migrate_fx(raw[i]);
        v.nums.push_back(item.real);
        v.nums_text.push_back(item.num_text);
    }
    return v;
}

inline rv_pdklib::rv_manifest_mvalue migrate_vec3(const rv_pdklib::fx_vec3 &p)
{
    const rv_pdklib::fx parts[ecs_array_items] = { p.x, p.y, p.z };
    return migrate_fx_list(parts, std::size(parts));
}

// Colour channels are whole numbers: their text is std::to_string.
inline rv_pdklib::rv_manifest_mvalue migrate_color(const rv_color &c)
{
    const int channels[] = { c.r, c.g, c.b };
    rv_pdklib::rv_manifest_mvalue v;
    v.kind = rv_pdklib::rv_manifest_value_kind::numbers;
    for (const int channel : channels) {
        v.nums.push_back(channel);
        v.nums_text.push_back(std::to_string(channel));
    }
    return v;
}

inline rv_pdklib::rv_manifest_mvalue migrate_text(const std::string &s)
{
    rv_pdklib::rv_manifest_mvalue v;
    v.kind = rv_pdklib::rv_manifest_value_kind::string;
    v.str = s;
    return v;
}

inline void migrate_put(migrate_fields &fields, const std::string &key, rv_pdklib::rv_manifest_mvalue value)
{
    rv_pdklib::rv_manifest_tree_entry e;
    e.key = key;
    e.value = std::move(value);
    fields.push_back(std::move(e));
}

// The vector is written only when it differs from its default.
inline void
migrate_put_vec3(migrate_fields &fields, const std::string &key, const rv_pdklib::fx_vec3 &value, const rv_pdklib::fx_vec3 &def)
{
    if (!migrate_same_vec3(value, def)) {
        migrate_put(fields, key, migrate_vec3(value));
    }
}

inline rv_scene_component migrate_component(const std::string &name, migrate_fields fields)
{
    rv_scene_component c;
    c.name = name;
    c.fields = std::move(fields);
    return c;
}

// Each generator appends the components of one kind to out; the object index is set by the caller.
using migrate_gen_fn = void (*)(const rv_scene_object &o, std::vector<rv_scene_component> &out);

inline void migrate_name(const rv_scene_object &o, std::vector<rv_scene_component> &out)
{
    migrate_fields fields;
    migrate_put(fields, migrate_field_of<ecs_component_name>(0), migrate_text(o.name));
    out.push_back(migrate_component(migrate_name_of<ecs_component_name>(), std::move(fields)));
}

// Always written; a field equal to its default is left out.
inline void migrate_transform(const rv_scene_object &o, std::vector<rv_scene_component> &out)
{
    const rv_scene_object defaults{};
    migrate_fields fields;
    migrate_put_vec3(fields, migrate_field_of<ecs_component_transform>(0), o.position, defaults.position);
    migrate_put_vec3(fields, migrate_field_of<ecs_component_transform>(1), o.rotation, defaults.rotation);
    migrate_put_vec3(fields, migrate_field_of<ecs_component_transform>(2), o.scale, defaults.scale);
    out.push_back(migrate_component(migrate_name_of<ecs_component_transform>(), std::move(fields)));
}

inline void migrate_camera(const rv_scene_object &, std::vector<rv_scene_component> &out)
{
    out.push_back(migrate_component(migrate_name_of<ecs_component_camera>(), {}));
}

inline void migrate_volume(const rv_scene_object &, std::vector<rv_scene_component> &out)
{
    out.push_back(migrate_component(migrate_name_of<ecs_component_volume>(), {}));
}

// No model without a mesh; tint is written only when it is not white.
inline void migrate_mesh(const rv_scene_object &o, std::vector<rv_scene_component> &out)
{
    if (o.mesh.empty()) {
        return;
    }
    migrate_fields fields;
    migrate_put(fields, migrate_field_of<ecs_component_model>(0), migrate_text(o.mesh));
    if (!migrate_is_white(o.tint)) {
        migrate_put(fields, migrate_field_of<ecs_component_model>(1), migrate_color(o.tint));
    }
    out.push_back(migrate_component(migrate_name_of<ecs_component_model>(), std::move(fields)));
}

inline void migrate_legacy(const rv_scene_object &o, std::vector<rv_scene_component> &out, const std::string &name)
{
    migrate_fields fields;
    migrate_put(fields, legacy_texture_key, migrate_text(o.texture));
    migrate_put(fields, legacy_uv_key, migrate_fx_list(o.uv, std::size(o.uv)));
    migrate_put(fields, legacy_tint_key, migrate_color(o.tint));
    migrate_put(fields, legacy_tess_key, migrate_fx(o.tess));
    out.push_back(migrate_component(name, std::move(fields)));
}

inline void migrate_quad(const rv_scene_object &o, std::vector<rv_scene_component> &out)
{
    migrate_legacy(o, out, legacy_quad_name);
}

inline void migrate_billboard(const rv_scene_object &o, std::vector<rv_scene_component> &out)
{
    migrate_legacy(o, out, legacy_billboard_name);
}

struct migrate_kind_row {
    const char *kind;
    migrate_gen_fn make;
};

// "group" has no row: it gives no kind component.
inline constexpr migrate_kind_row migrate_kind_rows[] = {
    { migrate_kind_camera, migrate_camera },
    { migrate_kind_volume, migrate_volume },
    { migrate_kind_mesh, migrate_mesh },
    { kind_quad, migrate_quad },
    { kind_billboard, migrate_billboard },
};

// All components of one form-1 object, in the order they are written: name, transform, then the kind's.
inline void migrate_object_components(const rv_scene_object &o, std::vector<rv_scene_component> &out)
{
    migrate_name(o, out);
    migrate_transform(o, out);
    for (const migrate_kind_row &row : migrate_kind_rows) {
        if (o.kind == row.kind) {
            row.make(o, out);
            return;
        }
    }
}

inline void migrate_list_add(std::string &list, bool drop, const char *name)
{
    if (!drop) {
        return;
    }
    list += list.empty() ? name : std::string(migrate_list_sep) + name;
}

// One warning for a sprite's legacy component; one for the fields the kind does not use, if any.
inline void migrate_object_warnings(const rv_scene_object &o, std::string &warnings)
{
    const bool sprite = migrate_is_sprite(o.kind);
    const rv_scene_object defaults{};
    if (sprite) {
        const char *legacy = o.kind == kind_quad ? legacy_quad_name : legacy_billboard_name;
        migrate_warn(warnings, o.id, std::string("migrated as the undescribed component '") + legacy + "'");
    }
    std::string dropped;
    // A mesh's tint is kept only with a model; a mesh is kept only on a mesh kind.
    const bool model = o.kind == migrate_kind_mesh && !o.mesh.empty();
    migrate_list_add(dropped, !sprite && !o.texture.empty(), legacy_texture_key);
    migrate_list_add(dropped, !sprite && !std::equal(std::begin(o.uv), std::end(o.uv), std::begin(defaults.uv)), legacy_uv_key);
    migrate_list_add(dropped, !sprite && !model && !migrate_is_white(o.tint), legacy_tint_key);
    migrate_list_add(dropped, !sprite && o.tess != defaults.tess, legacy_tess_key);
    migrate_list_add(dropped, o.kind != migrate_kind_mesh && !o.mesh.empty(), object_mesh_key);
    if (!dropped.empty()) {
        migrate_warn(warnings, o.id, "dropped " + dropped + " (not used by this object)");
    }
}

// Refuses (RV_ERR_INVAL) when an existing component of object index has a generated component's name.
inline int migrate_check_clash(const rv_scene &scene,
    int32_t index,
    const std::vector<rv_scene_component> &generated,
    std::string &warnings)
{
    const std::string &id = scene.objects[static_cast<std::size_t>(index)].id;
    for (const rv_scene_component &g : generated) {
        for (const rv_scene_component &c : scene.components) {
            if (c.object == index && c.name == g.name) {
                migrate_warn(warnings, id, "already has component '" + c.name + "'");
                return RV_ERR_INVAL;
            }
        }
    }
    return RV_OK;
}

// Form 1 becomes form 2: the object keeps id, parent, entity and extra; everything else is its default.
inline void migrate_reset(rv_scene_object &o)
{
    rv_scene_object form2{};
    form2.id = o.id;
    form2.parent = o.parent;
    form2.entity = o.entity;
    form2.extra = std::move(o.extra);
    o = std::move(form2);
}

// Rebuilds components: each object's generated ones, then its own, in the object order.
inline void migrate_apply(rv_scene &scene, std::vector<std::vector<rv_scene_component>> &generated)
{
    const int32_t count = static_cast<int32_t>(scene.objects.size());
    std::vector<rv_scene_component> out;
    for (int32_t i = 0; i < count; ++i) {
        rv_scene_object &o = scene.objects[static_cast<std::size_t>(i)];
        if (!o.kind.empty()) {
            migrate_reset(o);
        }
        for (rv_scene_component &g : generated[static_cast<std::size_t>(i)]) {
            g.object = i;
            out.push_back(std::move(g));
        }
        for (const rv_scene_component &c : scene.components) {
            if (c.object == i) {
                out.push_back(c);
            }
        }
    }
    // Components on an index outside objects are kept as they were.
    for (const rv_scene_component &c : scene.components) {
        if (c.object < 0 || c.object >= count) {
            out.push_back(c);
        }
    }
    scene.components = std::move(out);
}

inline bool migrate_has_form1(const rv_scene &scene)
{
    return std::any_of(scene.objects.begin(), scene.objects.end(), [](const rv_scene_object &o) {
        return !o.kind.empty();
    });
}

} // namespace detail

// Form 1 to form 2 in memory. RV_ERR_INVAL when a generated component's name is already on its object: the scene
// is then untouched and warnings names the object and the component. A document without form 1 is left as it is.
[[nodiscard]] inline int rv_scene_migrate(rv_scene &scene, std::string &warnings)
{
    if (!detail::migrate_has_form1(scene)) {
        return RV_OK;
    }
    std::vector<std::vector<rv_scene_component>> generated(scene.objects.size());
    std::string notes;
    for (std::size_t i = 0; i < scene.objects.size(); ++i) {
        const rv_scene_object &o = scene.objects[i];
        if (o.kind.empty()) {
            continue;
        }
        detail::migrate_object_components(o, generated[i]);
        detail::migrate_object_warnings(o, notes);
        const int clash = detail::migrate_check_clash(scene, static_cast<int32_t>(i), generated[i], warnings);
        if (clash != RV_OK) {
            return clash;
        }
    }
    detail::migrate_apply(scene, generated);
    warnings += notes;
    return RV_OK;
}

} // namespace rv_pdkkit
