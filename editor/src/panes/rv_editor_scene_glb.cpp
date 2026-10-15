// A .glb model for the scene viewport: read by pdklib's glTF reader, cached by path and mtime.

#include "panes/rv_editor_scene_glb.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "pdk/rv_err.h"
#include "panes/rv_editor_walk.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_icons.hpp"

namespace rv_editor
{

namespace
{

// A disc model name is a bare filename: no separator may appear in it.
constexpr std::string_view name_separators = "/\\";
// A colour byte's full scale, to map a part's RGB to 0..1.
constexpr float color_channel_max = 255.0f;

// One read of a .glb, kept until the file's mtime moves on. `rc` and `error` hold a failed read.
struct rv_editor_glb_entry {
    std::filesystem::file_time_type mtime;
    int rc = RV_OK;
    std::string error;
    std::shared_ptr<const rv_editor_glb_model> model; // null when the read failed
};

// Keyed by absolute path; file-local so no other translation unit can reach into it.
std::unordered_map<std::string, rv_editor_glb_entry> rv_editor_glb_cache;

// RGB bytes as 0..1, opaque.
ImVec4 rgb_unit(const std::array<uint8_t, 3> &c)
{
    return { c[0] / color_channel_max, c[1] / color_channel_max, c[2] / color_channel_max, 1.0f };
}

// Two 0..1 colours multiplied per channel, as the console's tint_colour does with 0..255 bytes.
ImVec4 modulate(const ImVec4 &a, const ImVec4 &b)
{
    return { a.x * b.x, a.y * b.y, a.z * b.z, 1.0f };
}

// A .glb's triangles, flat: the project's coordinates (left-handed, Y up, metres; the glTF reader
// has negated z) and uv (top-left origin, so no flip needed). `part_cpu` holds one picture per part
// (null: none).
std::vector<rv_editor_tri> rv_editor_glb_triangles(const rv_pdklib::rv_mppcmod &model,
    const rv_editor_affine &m,
    const std::vector<const rv_editor_cpu_image *> &part_cpu)
{
    std::vector<rv_editor_tri> tris;
    tris.reserve(model.triangles.size());
    for (const rv_pdklib::rv_mppcmod_triangle &t : model.triangles) {
        rv_editor_tri tri;
        for (std::size_t k = 0; k < std::size(tri.p); ++k) {
            const rv_pdklib::rv_mppcmod_vertex &v = model.vertices[t.index[k]];
            tri.p[k] = rv_editor_affine_point(m, { v.pos[0], v.pos[1], v.pos[2] });
            tri.uv[k] = { v.uv[0], v.uv[1] };
            tri.color[k] = rgb_unit(v.color);
        }
        tri.from_model = true;
        tris.push_back(tri);
    }
    // A part's triangles are a run of the index list; each takes the part's colour and picture.
    for (size_t p = 0; p < model.parts.size(); ++p) {
        const rv_pdklib::rv_mppcmod_part &part = model.parts[p];
        const ImVec4 part_color = rgb_unit(part.color);
        const rv_editor_cpu_image *cpu = p < part_cpu.size() ? part_cpu[p] : nullptr;
        const size_t end = std::min(static_cast<size_t>(part.first_tri) + part.tri_count, tris.size());
        for (size_t t = part.first_tri; t < end; ++t) {
            for (ImVec4 &c : tris[t].color) {
                c = modulate(c, part_color);
            }
            tris[t].cpu_tex = cpu;
        }
    }
    return tris;
}

// The part's texture name is "<image name>.mppctex"; empty for a name without that extension.
std::string part_image_name(const std::string &texture)
{
    constexpr std::string_view texture_ext = ".mppctex";
    const std::string_view name = texture;
    return name.ends_with(texture_ext) ? std::string(name.substr(0, name.size() - texture_ext.size())) : std::string();
}

// The index of the embedded image a part's texture names, or images.size() for none. The reader names
// each textured part's image, so a part matching none stays plain; an empty name matches nothing.
size_t part_image_index(const rv_editor_glb_model &glb, const rv_pdklib::rv_mppcmod_part &part)
{
    const std::string name = part_image_name(part.texture);
    const auto it = std::find_if(glb.images.begin(), glb.images.end(), [&name](const rv_pdklib::rv_mppcmod_gltf_image &image) {
        return !name.empty() && image.name == name;
    });
    return static_cast<size_t>(it - glb.images.begin());
}

// A .glb's embedded PNGs decoded to memory, and each part's one. `part` points into `images`, so a set
// lives until its file version is replaced in the cache: the first successful re-read after the file's
// mtime moved (or the file is gone) frees the old set.
struct rv_editor_glb_pictures {
    std::shared_ptr<const rv_editor_glb_model> model;
    // One per embedded PNG; w stays 0 when it did not decode.
    std::vector<rv_editor_cpu_image> images;
    // One per part; null for none or a failed decode.
    std::vector<const rv_editor_cpu_image *> part;
};

// Keyed by absolute path, as the model cache is. Not tied to a renderer: the CPU rasterizer reads it too.
std::unordered_map<std::string, rv_editor_glb_pictures> rv_editor_glb_picture_cache;

// The part pictures of `glb`, decoded once per file version. A replaced set is freed here, on the first
// successful re-read after the file changed, so its pointers are read before the next rv_editor_object_triangles call.
const rv_editor_glb_pictures &rv_editor_glb_part_pictures(const std::string &key,
    const std::shared_ptr<const rv_editor_glb_model> &glb)
{
    const auto found = rv_editor_glb_picture_cache.find(key);
    if (found != rv_editor_glb_picture_cache.end() && found->second.model == glb) {
        return found->second;
    }
    rv_editor_glb_pictures made;
    made.model = glb;
    made.images.resize(glb->images.size());
    for (size_t i = 0; i < glb->images.size(); ++i) {
        // A failed decode leaves w 0, so its parts stay plain; the loader has said why on stderr.
        rv_editor_cpu_image_load_png(glb->images[i].png, made.images[i]);
    }
    const auto it = rv_editor_glb_picture_cache.insert_or_assign(key, std::move(made)).first;
    for (const rv_pdklib::rv_mppcmod_part &part : glb->model.parts) {
        const size_t i = part_image_index(*glb, part);
        const bool decoded = i < it->second.images.size() && it->second.images[i].w > 0;
        it->second.part.push_back(decoded ? &it->second.images[i] : nullptr);
    }
    return it->second;
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
        rv_editor_glb_picture_cache.erase(key);
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
            model->pieces = rv_editor_model_pieces(model->model);
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
    const rv_editor_glb_pictures &pictures = rv_editor_glb_part_pictures(path.string(), glb);
    tris = rv_editor_glb_triangles(glb->model, m, pictures.part);
    return RV_OK;
}

} // namespace rv_editor
