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

// Scene object kind constants: the `kind` values of a scene object, as pdklib's scene format names them.
inline constexpr std::string_view kind_group = "group";
inline constexpr std::string_view kind_camera = "camera";
inline constexpr std::string_view kind_mesh = "mesh";
inline constexpr std::string_view kind_quad = "quad";
inline constexpr std::string_view kind_billboard = "billboard";
inline constexpr std::string_view kind_volume = "volume";

// A scene document as the editor edits it.
// The disc reads the same file with pdklib/rv_scene; the editor keeps its own
// model so that nothing it does not understand is lost on save.

using rv_editor_vec3 = std::array<double, 3>;
using rv_editor_uv = std::array<double, 4>;
using rv_editor_tint = std::array<int, 3>;

// Default tint, same as pdklib when a scene object lacks a `tint` key.
constexpr rv_editor_tint default_object_tint = { 255, 255, 255 };
// Default tess, same as pdklib when a scene object lacks a `tess` key.
constexpr double default_object_tess = 2.0;

struct rv_editor_scene_object {
    std::string id; // stable; never changes once given
    std::string name;
    std::string parent; // the parent's id, empty at the root
    std::string kind;   // "group", "camera", "mesh", "quad", "billboard" or "volume"
    rv_editor_vec3 position{ 0.0, 0.0, 0.0 };
    rv_editor_vec3 rotation{ 0.0, 0.0, 0.0 }; // degrees; R = Ry * Rx * Rz
    rv_editor_vec3 scale{ 1.0, 1.0, 1.0 };
    std::string mesh;
    std::string texture;
    rv_editor_uv uv{ 0.0, 0.0, 0.0, 0.0 };     // quad/billboard texture rect, pixels: u0,v0,u1,v1
    rv_editor_tint tint = default_object_tint; // quad/billboard modulation
    double tess = default_object_tess;         // quad/billboard subdivision density
    // Keys the editor does not know, written back as they were read.
    std::vector<rv_pdklib::rv_manifest_tree_entry> extra;
};

struct rv_editor_scene {
    std::filesystem::path path;
    // The PDK version the file was written for (rv_pdklib::rv_version_str for a fresh one);
    // a legacy `format = 1` header reads as 0.0, same as pdklib's scene reader.
    uint32_t version_major = static_cast<uint32_t>(RV_MPPC_VER_MAJOR);
    uint32_t version_minor = static_cast<uint32_t>(RV_MPPC_VER_MINOR);
    std::string preamble; // the comment lines before the first section, kept
    std::vector<rv_editor_scene_object> objects;
    std::vector<rv_pdklib::rv_manifest_tree_entry> scene_extra;      // unknown [scene] keys
    std::vector<rv_pdklib::rv_manifest_tree_section> other_sections; // unknown sections
    // Opened but not to be written: a non-compatible version, or content it cannot keep.
    std::string read_only;
};

// RV_OK on success; RV_ERR_INVAL if file unreadable or content malformed (reason in error).
// A version that is not rv_version_compatible opens read-only (the reason in scene.read_only),
// never rewritten.
int rv_editor_scene_load(const std::filesystem::path &path, rv_editor_scene &scene, std::string &error);

// The document as text in the dialect, unknown keys and sections included.
std::string rv_editor_scene_render(const rv_editor_scene &scene);

// RV_OK on success; RV_ERR_INVAL if read-only; RV_ERR_IO if file write fails.
// Writes through a temporary file and a rename.
int rv_editor_scene_save(const rv_editor_scene &scene, std::string &error);

// A new scene: a camera looking at one box, as the template's.
rv_editor_scene rv_editor_scene_make(const std::filesystem::path &path);

// An 8-hex-digit id no object of `scene` has.
std::string rv_editor_scene_new_id(const rv_editor_scene &scene);

// Index of the object with `id`, or -1.
int rv_editor_scene_find(const rv_editor_scene &scene, const std::string &id);

} // namespace rv_editor
