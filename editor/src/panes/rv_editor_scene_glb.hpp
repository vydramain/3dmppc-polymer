#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "pdklib/rv_models/rv_mppcmod_gltf.hpp"
#include "project/rv_editor_project.hpp"

namespace rv_editor
{

// A .glb model as the scene viewport reads it: the parsed container and its embedded PNGs.
struct rv_editor_glb_model {
    rv_pdklib::rv_mppcmod model;
    std::vector<rv_pdklib::rv_mppcmod_gltf_image> images;
};

// Reads the .glb at `path` (absolute), once per mtime. `out` shares the cached model, so a holder
// keeps it valid when the file changes or goes. RV_OK, or the code with the reason in `error`.
// Textures are not created here.
int rv_editor_glb_load(const std::filesystem::path &path, std::shared_ptr<const rv_editor_glb_model> &out, std::string &error);

// The .glb under the project's [models] patterns named `name` (a bare filename). RV_OK with `found`;
// RV_ERR_INVAL for a name with a folder, or one held by two files (both paths in `error`);
// RV_ERR_NOENT when none matches; RV_ERR_IO when the walk itself fails.
int rv_editor_glb_resolve_name(const rv_editor_project &project,
    const std::string &name,
    std::filesystem::path &found,
    std::string &error);

} // namespace rv_editor
