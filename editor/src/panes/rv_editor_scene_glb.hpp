#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "pdklib/rv_models/rv_mppcmod_gltf.hpp"
#include "panes/rv_editor_scene_draw.hpp"
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

// The .glb under [models] named `name` as triangles in scene space through `m`, RV_OK with `tris`
// written. Each triangle carries its part's colour and, with `renderer` non-null, its part's texture:
// the SDL textures are created on `renderer` and written to a cache (glb.cpp), once per file version
// and renderer. Otherwise the code with the reason in `error`, and `tris` is left alone: the caller
// falls back to the placeholder cube.
int rv_editor_glb_object_triangles(const rv_editor_project &project,
    const std::string &name,
    const rv_editor_affine &m,
    SDL_Renderer *renderer,
    std::vector<rv_editor_tri> &tris,
    std::string &error);

} // namespace rv_editor
