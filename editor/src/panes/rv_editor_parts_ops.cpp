// Delete parts and Detach: the picked pieces of the selected object's model, written to new glTF files.

#include "panes/rv_editor_parts_ops.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cgltf.h"

#include "pdk/rv_err.h"
#include "panes/rv_editor_scene_glb.hpp"
#include "scene/rv_editor_gltf_write.hpp"
#include "scene/rv_editor_model_stage.hpp"

namespace rv_editor
{

namespace
{

// Only a separate-file glTF is split: the writer's output is the same form, and a .glb has no file of its own.
constexpr std::string_view gltf_ext = ".gltf";
// A part's texture name is "<stem>-<image index>.mppctex", as the reader names it.
constexpr std::string_view texture_ext = ".mppctex";
// The suffix of a detached part's model base and of its object name.
constexpr std::string_view part_suffix = "_part";
// The model base for an object name with no character of [A-Za-z0-9_-].
constexpr std::string_view fallback_base = "model";
// Hierarchy numbers pieces from 1; a name taken twice gets its number from 2.
constexpr size_t first_piece_number = 1;
constexpr size_t first_copy_number = 2;

// What a part command works on: the object's index and model, the models folder and its pending
// folder, the texture URIs, and the triangles of the picked pieces (picked) and of the rest (kept).
struct rv_editor_parts_plan {
    size_t at = 0;
    std::string base;
    std::filesystem::path models_dir;
    std::filesystem::path pending;
    std::shared_ptr<const rv_editor_glb_model> glb;
    std::map<std::string, std::string> uris;
    std::vector<uint32_t> picked;
    std::vector<uint32_t> kept;
};

bool rv_editor_parts_name_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// The object name with only the characters a model name may hold; "model" when none is left.
std::string rv_editor_parts_base(const std::string &name)
{
    std::string base;
    for (const char c : name) {
        if (rv_editor_parts_name_char(c)) {
            base += c;
        }
    }
    if (base.empty()) {
        base = std::string(fallback_base);
    }
    return base;
}

// The object name of a detached part: "<name>_part", or "<name>_part_<n>" while the name is taken.
std::string rv_editor_parts_child_name(const rv_editor_scene &scene, const std::string &name)
{
    const auto taken = [&scene](const std::string &candidate) {
        return std::ranges::any_of(scene.objects, [&candidate](const rv_editor_scene_object &o) {
            return rv_editor_object_get_name(o) == candidate;
        });
    };
    std::string candidate = name + std::string(part_suffix);
    for (size_t n = first_copy_number; taken(candidate); ++n) {
        candidate = name + std::string(part_suffix) + "_" + std::to_string(n);
    }
    return candidate;
}

// The selected object's model: its .gltf under [models], read through the cache.
int rv_editor_parts_source(const rv_editor_project &project,
    const std::string &mesh,
    std::filesystem::path &source,
    std::shared_ptr<const rv_editor_glb_model> &glb,
    std::string &error)
{
    if (const int rc = rv_editor_glb_resolve_name(project, mesh, source, error); rc != RV_OK) {
        return rc;
    }
    if (source.extension().string() != gltf_ext) {
        error = "only glTF with separate files can be split: " + mesh;
        return RV_ERR_INVAL;
    }
    return rv_editor_glb_load(source, glb, error);
}

// The URI each image of the source .gltf names, made relative to the pending folder, under the texture
// name the reader gives that image. The URI is read beside the source; a path that cannot be made
// relative to the pending folder, or comes out absolute, is refused.
int rv_editor_parts_uris(const std::filesystem::path &source,
    const std::filesystem::path &pending,
    std::map<std::string, std::string> &uris,
    std::string &error)
{
    cgltf_options options{};
    cgltf_data *data = nullptr;
    const std::string file = source.string();
    if (cgltf_parse_file(&options, file.c_str(), &data) != cgltf_result_success) {
        error = "cannot parse " + file;
        return RV_ERR_INVAL;
    }
    const std::string stem = source.stem().string();
    const std::filesystem::path folder = source.parent_path();
    const std::filesystem::path base = pending.lexically_normal();
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const char *uri = data->images[i].uri;
        if (uri == nullptr) {
            continue;
        }
        // An empty or rooted URI would join to a path outside the source folder: refused before the join.
        if (uri[0] == '\0' || uri[0] == '/' || uri[0] == '\\') {
            error = std::string("image of ") + file + " has an empty or absolute URI";
            cgltf_free(data);
            return RV_ERR_INVAL;
        }
        const std::filesystem::path image = (folder / uri).lexically_normal();
        const std::filesystem::path rel = image.lexically_relative(base).lexically_normal();
        if (rel.empty() || rel.is_absolute() || rel.has_root_directory()) {
            error = std::string("image ") + uri + " of " + file + " cannot be reached from " + base.string();
            cgltf_free(data);
            return RV_ERR_INVAL;
        }
        uris[stem + "-" + std::to_string(i) + std::string(texture_ext)] = rel.generic_string();
    }
    cgltf_free(data);
    return RV_OK;
}

// Splits the model's triangles into the picked pieces (numbered from 1, as Hierarchy shows them) and the rest.
int rv_editor_parts_split(const rv_editor_glb_model &glb,
    const std::set<size_t> &picked_pieces,
    rv_editor_parts_plan &plan,
    std::string &error)
{
    std::vector<bool> is_picked(glb.quantized.triangles.size(), false);
    for (const size_t number : picked_pieces) {
        if (number < first_piece_number || number > glb.pieces.size()) {
            error = "picked piece " + std::to_string(number) + " is not in the model";
            return RV_ERR_INVAL;
        }
        for (const uint32_t t : glb.pieces[number - first_piece_number]) {
            is_picked[t] = true;
        }
    }
    for (size_t t = 0; t < is_picked.size(); ++t) {
        if (is_picked[t]) {
            plan.picked.push_back(static_cast<uint32_t>(t));
        } else {
            plan.kept.push_back(static_cast<uint32_t>(t));
        }
    }
    return RV_OK;
}

// Writes `tris` of the plan's model into the pending folder as `name`, creating the folder first.
int rv_editor_parts_write(const rv_editor_parts_plan &plan,
    const std::string &name,
    const std::vector<uint32_t> &tris,
    std::string &error)
{
    std::error_code ec;
    std::filesystem::create_directories(plan.pending, ec);
    if (ec) {
        error = "cannot create " + plan.pending.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    return rv_editor_gltf_write(plan.pending / name, plan.glb->quantized, plan.glb->source, tris, plan.uris, error);
}

// Prepares a part command: the selected object's model, its picks, and the split of its triangles.
// Refused with the reason when nothing is picked on it or the model cannot be split.
int rv_editor_parts_prepare(const rv_editor_project &project,
    rv_editor_scene_doc &doc,
    rv_editor_parts_plan &plan,
    std::string &error)
{
    const int at = rv_editor_scene_find(doc.scene, doc.selected);
    if (at < 0) {
        error = "nothing is selected";
        return RV_ERR_INVAL;
    }
    const rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(at)];
    if (rv_editor_object_get_kind(o) != kind_mesh) {
        error = "the selected object is not a mesh";
        return RV_ERR_INVAL;
    }
    std::filesystem::path source;
    std::shared_ptr<const rv_editor_glb_model> glb;
    if (const int rc = rv_editor_parts_source(project, rv_editor_object_get_mesh(o), source, glb, error); rc != RV_OK) {
        return rc;
    }
    rv_editor_scene_picks_check(doc, o, glb->pieces.size());
    if (doc.picked_owner != o.id || doc.picked_pieces.empty()) {
        error = "no pieces are picked on the selected object";
        return RV_ERR_INVAL;
    }
    const int target = rv_editor_model_target_dir(project.root, project.models_patterns, plan.models_dir, error);
    if (target != RV_OK) {
        return target;
    }
    plan.pending = rv_editor_model_pending_dir(plan.models_dir);
    if (const int rc = rv_editor_parts_uris(source, plan.pending, plan.uris, error); rc != RV_OK) {
        return rc;
    }
    plan.at = static_cast<size_t>(at);
    plan.base = rv_editor_parts_base(rv_editor_object_get_name(o));
    plan.glb = glb;
    return rv_editor_parts_split(*glb, doc.picked_pieces, plan, error);
}

} // namespace

int rv_editor_parts_delete(const rv_editor_project &project, rv_editor_scene_doc &doc, std::string &error)
{
    rv_editor_parts_plan plan;
    if (const int rc = rv_editor_parts_prepare(project, doc, plan, error); rc != RV_OK) {
        return rc;
    }
    if (plan.kept.empty()) {
        error = "these are all the pieces of the object: delete the object instead";
        return RV_ERR_INVAL;
    }
    std::string kept;
    if (const int rc = rv_editor_model_free_name(plan.models_dir, plan.base, kept, error); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_parts_write(plan, kept, plan.kept, error); rc != RV_OK) {
        return rc;
    }
    const std::string id = doc.selected;
    rv_editor_scene_step(doc);
    rv_editor_object_set_mesh(doc.scene.objects[plan.at], kept);
    rv_editor_scene_select(doc, id);
    return RV_OK;
}

int rv_editor_parts_detach(const rv_editor_project &project, rv_editor_scene_doc &doc, std::string &error)
{
    rv_editor_parts_plan plan;
    if (const int rc = rv_editor_parts_prepare(project, doc, plan, error); rc != RV_OK) {
        return rc;
    }
    if (plan.kept.empty()) {
        error = "these are all the pieces of the object: delete the object instead";
        return RV_ERR_INVAL;
    }
    std::string kept;
    std::string part;
    if (const int rc = rv_editor_model_free_name(plan.models_dir, plan.base, kept, error); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_model_free_name(plan.models_dir, plan.base + std::string(part_suffix), part, error);
        rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_parts_write(plan, part, plan.picked, error); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_parts_write(plan, kept, plan.kept, error); rc != RV_OK) {
        return rc;
    }
    const std::string parent = doc.selected;
    rv_editor_scene_step(doc);
    // The child sits where the parent's model sits: its vertices are already in the parent's space.
    rv_editor_scene_object &host = doc.scene.objects[plan.at];
    rv_editor_scene_object child;
    child.id = rv_editor_scene_new_id(doc.scene);
    rv_editor_object_set_name(child, rv_editor_parts_child_name(doc.scene, rv_editor_object_get_name(host)));
    child.parent = parent;
    rv_editor_object_set_kind(child, std::string(kind_mesh));
    rv_editor_object_set_mesh(child, part);
    rv_editor_object_set_tint(child, rv_editor_object_get_tint(host));
    rv_editor_object_set_mesh(host, kept);
    doc.scene.objects.push_back(child);
    rv_editor_scene_select(doc, child.id);
    return RV_OK;
}

} // namespace rv_editor
