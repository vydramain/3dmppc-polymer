#pragma once

// Opt-in C++ for a scene the editor just created: a header the disc can include
// to load it, and a comment explaining how. Written once; the editor never
// rewrites or regenerates it, and a user who does not want it never gets it.

#include <filesystem>
#include <string>
#include <string_view>

namespace rv_editor
{

// The header's text for scene `scene_name` (file scenes/<scene_name>.scene.toml).
std::string rv_editor_scene_codegen_text(std::string_view scene_name);

// Writes root/src/<id>_scene.hpp, <id> being scene_name as a C identifier
// (it is #included, so it must be one); creates src/ if missing. Never
// overwrites an existing file: returns RV_ERR_INVAL with `error` set and `written`
// untouched. On success (RV_OK) `written` holds the path written; RV_ERR_IO if write failed.
int rv_editor_scene_codegen_write(const std::filesystem::path &root, std::string_view scene_name,
    std::filesystem::path &written, std::string &error);

} // namespace rv_editor
