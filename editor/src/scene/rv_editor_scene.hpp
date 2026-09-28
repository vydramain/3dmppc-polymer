#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pdklib/rv_manifest/detail/rv_manifest_tree.hpp"

namespace rv_editor
{

// A scene document as the editor edits it.
// The disc reads the same file with pdklib/rv_scene; the editor keeps its own
// model so that nothing it does not understand is lost on save (SCN-04).

using rv_editor_vec3 = std::array<double, 3>;

struct rv_editor_scene_object
{
    std::string id;     // stable; never changes once given
    std::string name;
    std::string parent; // the parent's id, empty at the root
    std::string kind;   // "group", "camera" or "mesh"
    rv_editor_vec3 position{ 0.0, 0.0, 0.0 };
    rv_editor_vec3 rotation{ 0.0, 0.0, 0.0 }; // degrees; R = Ry * Rx * Rz
    rv_editor_vec3 scale{ 1.0, 1.0, 1.0 };
    std::string mesh;
    std::string texture;
    // Keys the editor does not know, written back as they were read.
    std::vector<rv_pdklib::rv_manifest_tree_entry> extra;
};

inline constexpr int64_t rv_editor_scene_format = 1;

struct rv_editor_scene
{
    std::filesystem::path path;
    int64_t format = rv_editor_scene_format;
    std::string preamble; // the comment lines before the first section, kept
    std::vector<rv_editor_scene_object> objects;
    std::vector<rv_pdklib::rv_manifest_tree_entry> scene_extra;       // unknown [scene] keys
    std::vector<rv_pdklib::rv_manifest_tree_section> other_sections; // unknown sections
    // Opened but not to be written: a newer format, or content it cannot keep.
    std::string read_only;
};

// False with the reason when the file does not read at all. A newer format opens
// read-only (the reason in scene.read_only), never rewritten (SCN-04).
bool rv_editor_scene_load(const std::filesystem::path &path, rv_editor_scene &scene, std::string &error);

// The document as text in the dialect, unknown keys and sections included.
std::string rv_editor_scene_render(const rv_editor_scene &scene);

// Writes it through a temporary file and a rename; refuses a read-only one.
bool rv_editor_scene_save(const rv_editor_scene &scene, std::string &error);

// A new scene: a camera looking at one box, as the template's.
rv_editor_scene rv_editor_scene_make(const std::filesystem::path &path);

// An 8-hex-digit id no object of `scene` has.
std::string rv_editor_scene_new_id(const rv_editor_scene &scene);

// Index of the object with `id`, or -1.
int rv_editor_scene_find(const rv_editor_scene &scene, const std::string &id);

} // namespace rv_editor
