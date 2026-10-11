// A .glb model for the scene viewport: read by pdklib's glTF reader, cached by path and mtime.

#include "panes/rv_editor_scene_glb.hpp"

#include <cstddef>
#include <iterator>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "pdk/rv_err.h"
#include "panes/rv_editor_walk.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// A disc model name is a bare filename: no separator may appear in it.
constexpr std::string_view name_separators = "/\\";

// One read of a .glb, kept until the file's mtime moves on. `rc` and `error` hold a failed read.
struct rv_editor_glb_entry {
    std::filesystem::file_time_type mtime;
    int rc = RV_OK;
    std::string error;
    std::shared_ptr<const rv_editor_glb_model> model; // null when the read failed
};

// Keyed by absolute path; file-local so no other translation unit can reach into it.
std::unordered_map<std::string, rv_editor_glb_entry> rv_editor_glb_cache;

// A .glb's triangles, flat: glTF's own coordinates (Y up, metres) and uv (top-left origin, so
// no flip as .obj needs). Colour and texture parts are not drawn yet.
std::vector<rv_editor_tri> rv_editor_glb_triangles(const rv_pdklib::rv_mppcmod &model, const rv_editor_affine &m)
{
    std::vector<rv_editor_tri> tris;
    tris.reserve(model.triangles.size());
    for (const rv_pdklib::rv_mppcmod_triangle &t : model.triangles) {
        rv_editor_tri tri;
        for (std::size_t k = 0; k < std::size(tri.p); ++k) {
            const rv_pdklib::rv_mppcmod_vertex &v = model.vertices[t.index[k]];
            tri.p[k] = rv_editor_affine_point(m, { v.pos[0], v.pos[1], v.pos[2] });
            tri.uv[k] = { v.uv[0], v.uv[1] };
        }
        tris.push_back(tri);
    }
    return tris;
}

} // namespace

int rv_editor_glb_load(const std::filesystem::path &path, std::shared_ptr<const rv_editor_glb_model> &out, std::string &error)
{
    out.reset();
    if (!path.is_absolute()) {
        const std::string text = path.string();
        error = rv_editor_text_format("project.model_not_absolute", std::make_format_args(text));
        return RV_ERR_INVAL;
    }
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(path, ec);
    const std::string key = path.string();
    if (ec) {
        const std::string name = path.filename().string();
        error = rv_editor_text_format("project.model_not_found", std::make_format_args(name));
        rv_editor_glb_cache.erase(key);
        return RV_ERR_NOENT;
    }
    auto it = rv_editor_glb_cache.find(key);
    if (it == rv_editor_glb_cache.end() || it->second.mtime != mtime) {
        rv_editor_glb_entry entry;
        entry.mtime = mtime;
        auto model = std::make_shared<rv_editor_glb_model>();
        const std::string stem = path.stem().string();
        entry.rc = rv_pdklib::rv_mppcmod_read_gltf(key, stem, model->model, model->images, entry.error);
        if (entry.rc == RV_OK) {
            entry.rc = rv_pdklib::rv_mppcmod_validate(model->model, entry.error);
        }
        if (entry.rc == RV_OK) {
            entry.model = std::move(model);
        }
        it = rv_editor_glb_cache.insert_or_assign(key, std::move(entry)).first;
    }
    const rv_editor_glb_entry &slot = it->second;
    if (slot.rc != RV_OK) {
        error = slot.error;
        return slot.rc;
    }
    out = slot.model;
    return RV_OK;
}

int rv_editor_glb_resolve_name(const rv_editor_project &project,
    const std::string &name,
    std::filesystem::path &found,
    std::string &error)
{
    if (name.find_first_of(name_separators) != std::string::npos) {
        error = rv_editor_text_format("project.model_has_folder", std::make_format_args(name));
        return RV_ERR_INVAL;
    }
    const rv_editor_walk_result walk = rv_editor_walk_name(project, rv_editor_name_section::models, false, name);
    if (walk.ec) {
        const std::string message = walk.ec.message();
        const int code = walk.ec.value();
        error = rv_editor_text_format("project.walk_failed", std::make_format_args(name, message, code));
        return RV_ERR_IO;
    }
    if (walk.matches.empty()) {
        error = rv_editor_text_format("project.model_no_file", std::make_format_args(name));
        return RV_ERR_NOENT;
    }
    if (walk.matches.size() > 1) {
        const std::string first = walk.matches[0].string();
        const std::string second = walk.matches[1].string();
        error = rv_editor_text_format("project.model_ambiguous", std::make_format_args(name, first, second));
        return RV_ERR_INVAL;
    }
    found = walk.matches.front();
    return RV_OK;
}

// A .glb named by its bare filename: resolved under [models] and read once per mtime. Returns the
// code with the reason in `error`; the placeholder cube fallback is the caller's.
int rv_editor_glb_object_triangles(const rv_editor_project &project,
    const std::string &name,
    const rv_editor_affine &m,
    std::vector<rv_editor_tri> &tris,
    std::string &error)
{
    std::filesystem::path path;
    if (const int rc = rv_editor_glb_resolve_name(project, name, path, error); rc != RV_OK) {
        return rc;
    }
    std::shared_ptr<const rv_editor_glb_model> glb;
    if (const int rc = rv_editor_glb_load(path, glb, error); rc != RV_OK) {
        return rc;
    }
    tris = rv_editor_glb_triangles(glb->model, m);
    return RV_OK;
}

} // namespace rv_editor
