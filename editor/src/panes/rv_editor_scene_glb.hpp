#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_models/rv_mppcmod_gltf.hpp"
#include "pdklib/rv_models/rv_mppcmod_int.hpp"
#include "panes/rv_editor_scene_draw.hpp"
#include "project/rv_editor_project.hpp"
#include "scene/rv_editor_model_pieces.hpp"

namespace rv_pdklib
{
struct rv_gte_matrix;
} // namespace rv_pdklib

namespace rv_editor
{

// A .glb model as the scene viewport reads it: the parsed container, its quantized model, its embedded PNGs and its pieces.
struct rv_editor_glb_model {
    // The model as quantized: what the viewport draws and Parts picks from.
    rv_pdklib::rv_mppcmod_int_model quantized;
    // The glTF floats before quantization; index i is quantized vertex i. Kept for Separate only.
    std::vector<rv_pdklib::rv_mppcmod_gltf_source_vertex> source;
    std::vector<rv_pdklib::rv_mppcmod_gltf_image> images;
    // Set once per read, with the model; the triangles of each piece.
    std::vector<rv_editor_piece> pieces;
};

// Q20.12 raw units per metre: the exact divisor from an integer scene point to the viewport's double metres;
// the bridge until the viewport draws integer points.
inline constexpr double rv_editor_gte_units_per_metre = static_cast<double>(rv_pdklib::gte_one);

// Vertex `vertex` of `model` through `world`, the chain the drawn mesh uses: its Q20.12 point in `q`.
// RV_ERR_* from rv_gte_vertex when a component leaves int32; `q` is then untouched.
int rv_editor_glb_vertex_in_world(const rv_pdklib::rv_mppcmod_int_model &model,
    const rv_pdklib::rv_gte_matrix &world,
    std::size_t vertex,
    rv_pdklib::fx_vec3 &q);

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

// The .glb under [models] named `name` as triangles in scene space through `world` (the console's integer
// chain, Q20.12 in metres after the divide). Each triangle carries its part's colour and its part's picture
// in memory (cpu_tex). The pictures are freed by the first successful re-read after the file's mtime moved
// (or when the file is gone), so a cpu_tex is read before the next rv_editor_object_triangles call. A triangle
// with a vertex outside int32 is not drawn: RV_OK still, with `error` naming the model and the count (a
// warning). On failure: the code with the reason in `error`, and `tris` is left alone; the caller falls back
// to the placeholder cube.
int rv_editor_glb_object_triangles(const rv_editor_project &project,
    const std::string &name,
    const rv_pdklib::rv_gte_matrix &world,
    std::vector<rv_editor_tri> &tris,
    std::string &error);

} // namespace rv_editor
