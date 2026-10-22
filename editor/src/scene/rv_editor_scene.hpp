#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pdklib/rv_manifest/detail/rv_manifest_tree.hpp"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_editor
{

// Scene object kind constants: the `kind` values of a scene object, as pdkkit's scene format names them.
inline constexpr std::string_view kind_group = "group";
inline constexpr std::string_view kind_camera = "camera";
inline constexpr std::string_view kind_mesh = "mesh";
inline constexpr std::string_view kind_quad = "quad";
inline constexpr std::string_view kind_billboard = "billboard";
inline constexpr std::string_view kind_volume = "volume";

// The component that marks an object hidden: its row is dimmed, and it is drawn as an outline or not at all.
inline constexpr std::string_view component_hidden = "hidden";

// A scene document as the editor edits it: pdkkit's rv_scene with numbers as doubles and
// parents as ids. The file is read and written by pdkkit/scene. Unknown keys and sections
// are written back; the legacy `format` key is not (the file is written with `version`).

using rv_editor_vec3 = std::array<double, 3>;
using rv_editor_uv = std::array<double, 4>;
using rv_editor_tint = std::array<int, 3>;

// Default tint, same as pdkkit when a scene object lacks a `tint` key.
constexpr rv_editor_tint default_object_tint = { 255, 255, 255 };
// Default tess, same as pdkkit when a scene object lacks a `tess` key.
constexpr double default_object_tess = 2.0;

// A [[component]] of its object: the component type's name and its fields in file order.
struct rv_editor_scene_component {
    std::string name;
    std::vector<rv_pdklib::rv_manifest_tree_entry> fields;
};

struct rv_editor_scene_object {
    std::string id; // stable; never changes once given
    std::string name;
    std::string parent; // the parent's id, empty at the root
    std::string kind;   // "group", "camera", "mesh", "quad", "billboard" or "volume"
    std::string entity; // entity type name, empty: the base type
    rv_editor_vec3 position{ 0.0, 0.0, 0.0 };
    rv_editor_vec3 rotation{ 0.0, 0.0, 0.0 }; // degrees; R = Ry * Rx * Rz
    rv_editor_vec3 scale{ 1.0, 1.0, 1.0 };
    std::string mesh;
    std::string texture;
    rv_editor_uv uv{ 0.0, 0.0, 0.0, 0.0 };     // quad/billboard texture rect, pixels: u0,v0,u1,v1
    rv_editor_tint tint = default_object_tint; // quad/billboard modulation
    double tess = default_object_tess;         // quad/billboard subdivision density
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
    // Non-empty: save refuses to write. Load sets it for an object with no kind (component form, not edited yet).
    // An unreadable, malformed or incompatible file fails to open.
    std::string read_only;
};

// RV_OK on success; RV_ERR_INVAL if the file is unreadable, malformed or of a version
// that is not rv_version_compatible (reason in error).
int rv_editor_scene_load(const std::filesystem::path &path, rv_editor_scene &scene, std::string &error);

// Numbers are rounded to the nearest 1/4096. RV_OK on success; RV_ERR_INVAL if read-only, if a
// number lies outside Q20.12, or if tess is not above zero after rounding (reason in error);
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

// New code reaches an object's data through these accessors; the storage behind them will change
// (raw Q20.12, then pdkkit components), so getters return by value. The fields stay public until then.
// id, parent, entity, components and extra are the object's structure in both forms and stay direct
// fields, so a whole-object copy (duplicate) stays valid.
std::string rv_editor_object_get_name(const rv_editor_scene_object &o);
void rv_editor_object_set_name(rv_editor_scene_object &o, const std::string &value);
std::string rv_editor_object_get_kind(const rv_editor_scene_object &o);
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
rv_editor_uv rv_editor_object_get_uv(const rv_editor_scene_object &o);
void rv_editor_object_set_uv(rv_editor_scene_object &o, const rv_editor_uv &value);
rv_editor_tint rv_editor_object_get_tint(const rv_editor_scene_object &o);
void rv_editor_object_set_tint(rv_editor_scene_object &o, const rv_editor_tint &value);
double rv_editor_object_get_tess(const rv_editor_scene_object &o);
void rv_editor_object_set_tess(rv_editor_scene_object &o, double value);

} // namespace rv_editor
