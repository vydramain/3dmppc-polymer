#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pdklib/rv_manifest/detail/rv_manifest_tree.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_pdkkit
{
struct ecs_component_desc;
struct ecs_component_record;
} // namespace rv_pdkkit

namespace rv_editor
{

// Scene object kind constants: the kind an object has from its components, as pdkkit's scene format names them.
inline constexpr std::string_view kind_group = "group";
inline constexpr std::string_view kind_camera = "camera";
inline constexpr std::string_view kind_mesh = "mesh";
inline constexpr std::string_view kind_quad = "quad";
inline constexpr std::string_view kind_billboard = "billboard";
inline constexpr std::string_view kind_volume = "volume";

// The component that marks an object hidden: its row is dimmed, and it is drawn as an outline or not at all.
inline constexpr std::string_view component_hidden = "hidden";

// The component that keeps an object's back faces in the filled viewport, as the game does (pdkkit's two_sided).
inline constexpr std::string_view component_two_sided = "two_sided";

// Log tag of the scene's warnings, shared by the scene and object sources.
inline constexpr const char *scene_log_tag = "scene";

// A scene document as the editor edits it: pdkkit's rv_scene in form 2, where an object's data lives in its
// components. Unknown keys and sections are written back; the legacy `format` key is not (the file is written
// with `version`).

using rv_editor_vec3 = std::array<double, 3>;
using rv_editor_tint = std::array<int, 3>;

// Default tint, same as pdkkit when a model or sprite lacks a `tint` field.
constexpr rv_editor_tint default_object_tint = { 255, 255, 255 };

// A [[component]] of its object: the component type's name and its fields in file order.
struct rv_editor_scene_component {
    std::string name;
    std::vector<rv_pdklib::rv_manifest_tree_entry> fields;
};

struct rv_editor_scene_object {
    std::string id; // stable; never changes once given
    std::string parent; // the parent's id, empty at the root
    std::string entity; // entity type name, empty: the base type
    std::vector<rv_editor_scene_component> components; // in file order
    // Keys the editor does not know, written back as they were read.
    std::vector<rv_pdklib::rv_manifest_tree_entry> extra;
};

struct rv_editor_scene {
    std::filesystem::path path;
    // The PDK version the file was written for (rv_pdklib::rv_version_str for a fresh one);
    // a legacy `format = 1` header reads as 0.0, same as pdkkit's scene reader.
    uint32_t version_major = static_cast<uint32_t>(RV_MPPC_VER_MAJOR);
    uint32_t version_minor = static_cast<uint32_t>(RV_MPPC_VER_MINOR);
    std::string preamble; // the comment lines before the first section, kept
    std::vector<rv_editor_scene_object> objects;
    std::vector<rv_pdklib::rv_manifest_tree_entry> scene_extra;      // unknown [scene] keys
    std::vector<rv_pdklib::rv_manifest_tree_section> other_sections; // unknown sections
    // Non-empty: save refuses to write and the panes show why. Load sets none; an unreadable file fails to load.
    std::string read_only;
};

// RV_OK on success; RV_ERR_INVAL if the file is unreadable, malformed or of a version that is not
// rv_version_compatible, or a number or text of a data component does not read (reason in error).
// A form-1 file is migrated to form 2 on the way in.
int rv_editor_scene_load(const std::filesystem::path &path, rv_editor_scene &scene, std::string &error);

// RV_OK on success; RV_ERR_INVAL if read-only, if a data component does not read or tess is not above zero (reason in error);
// RV_ERR_IO if file write fails. Writes through a temporary file and a rename.
int rv_editor_scene_save(const rv_editor_scene &scene, std::string &error);

// A new scene: a camera looking at one box, as the template's.
rv_editor_scene rv_editor_scene_make(const std::filesystem::path &path);

// An 8-hex-digit id no object of `scene` has.
std::string rv_editor_scene_new_id(const rv_editor_scene &scene);

// Index of the object with `id`, or -1.
int rv_editor_scene_find(const rv_editor_scene &scene, const std::string &id);

// The object's first component named `name`, or nullptr.
const rv_editor_scene_component *rv_editor_scene_component_find(const rv_editor_scene_object &o, std::string_view name);

// True if the object has a component named `name`.
bool rv_editor_scene_has_component(const rv_editor_scene_object &o, std::string_view name);

// RV_OK, or RV_ERR_INVAL with the reason when a data component of o does not read; load and save check every object.
int rv_editor_scene_object_check(const rv_editor_scene_object &o, std::string &error);

// True for the components the accessors below own (name, transform, model, camera, volume, legacy_quad, legacy_billboard).
bool rv_editor_scene_component_is_data(std::string_view name);

// True for a kind marker's component (camera, volume, model, legacy_quad, legacy_billboard): its presence gives the kind.
bool rv_editor_scene_component_is_kind(std::string_view name);

// New code reaches an object's data through these accessors, which read and write its components; getters
// return by value. A setter given a number outside Q20.12 keeps the field and logs one warning.
// id, parent, entity, components and extra are the object's structure and stay direct fields,
// so a whole-object copy (duplicate) stays valid.
std::string rv_editor_object_get_name(const rv_editor_scene_object &o);
void rv_editor_object_set_name(rv_editor_scene_object &o, const std::string &value);
std::string rv_editor_object_get_kind(const rv_editor_scene_object &o);
// Creation only: adds the kind's marker, never removes one; a kind the object does not end up with is logged.
// A mesh's marker is its model component, so the object is a mesh as soon as the model is added, source or not.
void rv_editor_object_set_kind(rv_editor_scene_object &o, const std::string &value);
rv_editor_vec3 rv_editor_object_get_position(const rv_editor_scene_object &o);
void rv_editor_object_set_position(rv_editor_scene_object &o, const rv_editor_vec3 &value);
rv_editor_vec3 rv_editor_object_get_rotation(const rv_editor_scene_object &o);
void rv_editor_object_set_rotation(rv_editor_scene_object &o, const rv_editor_vec3 &value);
rv_editor_vec3 rv_editor_object_get_scale(const rv_editor_scene_object &o);
void rv_editor_object_set_scale(rv_editor_scene_object &o, const rv_editor_vec3 &value);
std::string rv_editor_object_get_mesh(const rv_editor_scene_object &o);
void rv_editor_object_set_mesh(rv_editor_scene_object &o, const std::string &value);
std::string rv_editor_object_get_texture(const rv_editor_scene_object &o);
void rv_editor_object_set_texture(rv_editor_scene_object &o, const std::string &value);
rv_editor_tint rv_editor_object_get_tint(const rv_editor_scene_object &o);
void rv_editor_object_set_tint(rv_editor_scene_object &o, const rv_editor_tint &value);
double rv_editor_object_get_tess(const rv_editor_scene_object &o);
void rv_editor_object_set_tess(rv_editor_scene_object &o, double value);

// Q20.12 twins of the accessors above: raw values, no conversion. Transitional; callers move to them and the double
// ones go, then the suffix goes. A set refuses only a sprite-less object (tess) or a tess not above zero;
// position, rotation and scale never refuse, since fx holds every Q20.12 value. The double setters still refuse
// input outside Q20.12.
rv_pdklib::fx_vec3 rv_editor_object_get_position_fx(const rv_editor_scene_object &o);
void rv_editor_object_set_position_fx(rv_editor_scene_object &o, const rv_pdklib::fx_vec3 &value);
rv_pdklib::fx_vec3 rv_editor_object_get_rotation_fx(const rv_editor_scene_object &o);
void rv_editor_object_set_rotation_fx(rv_editor_scene_object &o, const rv_pdklib::fx_vec3 &value);
rv_pdklib::fx_vec3 rv_editor_object_get_scale_fx(const rv_editor_scene_object &o);
void rv_editor_object_set_scale_fx(rv_editor_scene_object &o, const rv_pdklib::fx_vec3 &value);
rv_pdklib::fx rv_editor_object_get_tess_fx(const rv_editor_scene_object &o);
void rv_editor_object_set_tess_fx(rv_editor_scene_object &o, rv_pdklib::fx value);

// One row of a component in the Inspector. A described field has its type name and text: the file's value, or the
// default's with is_default. A field the description does not name, and every field of an undescribed component,
// has the text as the file holds it and no type.
struct rv_editor_scene_field_row {
    std::string name;
    std::string type; // the type's name from ecs_field_types; empty when undescribed
    std::string text;
    bool described = false;
    bool is_default = false; // the file has no value for the field: text is the default
    std::string note;        // "repeated field" on a second key of a described field; empty otherwise
    int entry = -1;          // index of the row's entry in c.fields; -1 when the file has none
};

// The description of the component named name: the snapshot's records when records is given (the snapshot is loaded),
// else the built-ins'. nullptr when the one that applies does not describe it.
const rv_pdkkit::ecs_component_desc *rv_editor_scene_desc_find(std::string_view name,
    const std::vector<rv_pdkkit::ecs_component_record> *records);

// The name of a description, as a view.
std::string_view rv_editor_scene_desc_name(const rv_pdkkit::ecs_component_desc &desc);

// The rows of c: the described fields in description order, then the fields desc does not name, in file order.
// A null desc leaves every field undescribed.
std::vector<rv_editor_scene_field_row> rv_editor_scene_field_rows(const rv_editor_scene_component &c,
    const rv_pdkkit::ecs_component_desc *desc);

// Sets field key of c to text, read by pdkkit's field parser (the console's rules). A value whose bytes equal the
// default's removes the field; any other is written as its value. RV_ERR_NOENT if desc does not name key;
// RV_ERR_INVAL with the reason if the text does not read, and c is then unchanged.
int rv_editor_scene_field_set(rv_editor_scene_component &c,
    const rv_pdkkit::ecs_component_desc &desc,
    std::string_view key,
    const std::string &text,
    std::string &error);

// Removes the entry at index of c.fields (a row's entry), so a repeated key keeps its other entries. RV_ERR_INVAL with
// the reason in error if index is not an entry of c; c is then unchanged.
int rv_editor_scene_field_remove_entry(rv_editor_scene_component &c, int index, std::string &error);

// Removes every entry of key from c, so the default applies again: the Reset of a described field.
void rv_editor_scene_field_reset(rv_editor_scene_component &c, std::string_view key);

} // namespace rv_editor
