// The Scene viewport's Parts mode: a click picks one piece of a model (its triangles joined by
// shared vertices), and the selected object's picked pieces are drawn over its edges.

#include "panes/rv_editor_scene_draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "panes/rv_editor_scene_glb.hpp"
#include "project/rv_editor_project.hpp"
#include "scene/rv_editor_scene_edit.hpp"

namespace rv_editor
{

namespace
{

// Screen triangles thinner than this (px squared) are not picked: their weights are meaningless.
constexpr double sliver_area = 1e-3;
// A picked piece's edges are drawn this wide, over the object's edges.
constexpr float line_width_picked = 2.0f;

// The model of the mesh named `name`, read through the cache; null when it does not load. Its error
// is already shown by the object's edges in the viewport's status line.
std::shared_ptr<const rv_editor_glb_model> rv_editor_parts_model(const rv_editor_project &project, const std::string &name)
{
    std::shared_ptr<const rv_editor_glb_model> glb;
    std::filesystem::path path;
    std::string error;
    if (!rv_editor_is_model_file_name(name)) {
        return nullptr;
    }
    if (rv_editor_glb_resolve_name(project, name, path, error) != RV_OK) {
        return nullptr;
    }
    if (rv_editor_glb_load(path, glb, error) != RV_OK) {
        return nullptr;
    }
    return glb;
}

// Twice the signed area of the screen triangle a, b, c; the sign tells the winding.
double rv_editor_parts_cross(ImVec2 a, ImVec2 b, ImVec2 c)
{
    const double ax = a.x, ay = a.y;
    const double bx = b.x, by = b.y;
    const double cx = c.x, cy = c.y;
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

// Barycentric weights of p in the screen triangle a, b, c (they sum to 1); false when p lies outside
// or the triangle is a sliver. Dividing by the triangle's own area makes the test wind-independent.
bool rv_editor_parts_weights(ImVec2 p, ImVec2 a, ImVec2 b, ImVec2 c, double (&w)[tri_vertices])
{
    const double area = rv_editor_parts_cross(a, b, c);
    if (std::fabs(area) < sliver_area) {
        return false;
    }
    w[0] = rv_editor_parts_cross(b, c, p) / area;
    w[1] = rv_editor_parts_cross(c, a, p) / area;
    w[2] = rv_editor_parts_cross(a, b, p) / area;
    return w[0] >= 0.0 && w[1] >= 0.0 && w[2] >= 0.0;
}

// The triangle of `glb`, placed by `m`, nearest the camera among those under `at`; -1 for none. Its view
// depth at `at` comes back in `depth`: 1/z is linear over the screen, so it is interpolated by the same
// weights. A triangle with a corner behind the near plane is skipped.
int rv_editor_parts_nearest(const rv_editor_glb_model &glb,
    const rv_editor_view &v,
    const rv_editor_affine &m,
    ImVec2 at,
    double &depth)
{
    int best = -1;
    for (size_t t = 0; t < glb.model.triangles.size(); ++t) {
        const rv_pdklib::rv_mppcmod_triangle &tri = glb.model.triangles[t];
        vec3 p[std::size(tri.index)];
        ImVec2 s[std::size(tri.index)];
        bool in_front = true;
        for (size_t k = 0; k < std::size(tri.index); ++k) {
            const rv_pdklib::rv_mppcmod_vertex &vx = glb.model.vertices[tri.index[k]];
            p[k] = rv_editor_affine_point(m, { vx.pos[0], vx.pos[1], vx.pos[2] });
            in_front = in_front && v.point(p[k], s[k]);
        }
        if (!in_front) {
            continue;
        }
        double w[tri_vertices];
        if (!rv_editor_parts_weights(at, s[0], s[1], s[2], w)) {
            continue;
        }
        double inv_z = 0.0;
        for (size_t k = 0; k < std::size(tri.index); ++k) {
            inv_z += w[k] / v.to_view(p[k])[2];
        }
        const double z = 1.0 / inv_z;
        if (best < 0 || z < depth) {
            best = static_cast<int>(t);
            depth = z;
        }
    }
    return best;
}

// The 1-based number of the piece holding triangle `tri`. rv_editor_model_pieces puts every triangle
// in exactly one piece, so the search always finds it; the 0 after the loop is never returned.
size_t rv_editor_parts_piece_of(const rv_editor_glb_model &glb, uint32_t tri)
{
    for (size_t p = 0; p < glb.pieces.size(); ++p) {
        if (std::find(glb.pieces[p].begin(), glb.pieces[p].end(), tri) != glb.pieces[p].end()) {
            return p + 1;
        }
    }
    return 0;
}

} // namespace

int rv_editor_parts_pick(const rv_editor_scene_doc &doc,
    const rv_editor_project &project,
    const rv_editor_view &v,
    ImVec2 at,
    std::string &owner,
    size_t &piece,
    std::shared_ptr<const rv_editor_glb_model> &model)
{
    const rv_editor_scene &scene = doc.scene;
    std::shared_ptr<const rv_editor_glb_model> best_glb;
    size_t best_object = 0;
    int best_tri = -1;
    double best_depth = 0.0;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const rv_editor_scene_object &o = scene.objects[i];
        if (o.kind != kind_mesh || rv_editor_scene_visibility(doc, o) == rv_editor_visibility::absent) {
            continue;
        }
        const std::shared_ptr<const rv_editor_glb_model> glb = rv_editor_parts_model(project, o.mesh);
        if (!glb) {
            continue;
        }
        double depth = 0.0;
        const int tri = rv_editor_parts_nearest(*glb, v, rv_editor_scene_world(scene, static_cast<int>(i)), at, depth);
        if (tri >= 0 && (best_tri < 0 || depth < best_depth)) {
            best_glb = glb;
            best_object = i;
            best_tri = tri;
            best_depth = depth;
        }
    }
    if (best_tri < 0) {
        return RV_ERR_NOENT;
    }
    owner = scene.objects[best_object].id;
    piece = rv_editor_parts_piece_of(*best_glb, static_cast<uint32_t>(best_tri));
    model = best_glb;
    return RV_OK;
}

void rv_editor_parts_click(rv_editor_scene_doc &doc,
    const rv_editor_project &project,
    const rv_editor_view &v,
    ImVec2 at,
    bool shift)
{
    std::string owner;
    size_t number = 0;
    // A miss clears the selection, as an object click on empty space does; Shift leaves it.
    std::shared_ptr<const rv_editor_glb_model> glb;
    const int rc = rv_editor_parts_pick(doc, project, v, at, owner, number, glb);
    if (rc != RV_OK && !shift) {
        rv_editor_scene_select(doc, "");
    }
    if (rc != RV_OK) {
        return;
    }
    // The id came from this scene a moment ago, so it is found.
    const rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(rv_editor_scene_find(doc.scene, owner))];
    rv_editor_scene_picks_check(doc, o, glb->pieces.size());
    // Shift on the same object toggles among its picks; anything else starts over on the object.
    const bool keep = shift && doc.picked_owner == owner;
    if (!keep) {
        rv_editor_scene_select(doc, owner);
    }
    doc.picked_owner = owner;
    doc.picked_mesh = o.mesh;
    doc.picked_count = glb->pieces.size();
    if (doc.picked_pieces.erase(number) == 0) {
        doc.picked_pieces.insert(number);
    }
}

void rv_editor_parts_highlight(ImDrawList *dl,
    const rv_editor_view &v,
    const rv_editor_scene_doc &doc,
    const rv_editor_project &project,
    ImU32 color)
{
    // Nothing picked, or the picks belong to another object or to a gone one: nothing to draw.
    const int at = rv_editor_scene_find(doc.scene, doc.picked_owner);
    if (doc.picked_pieces.empty() || doc.picked_owner != doc.selected || at < 0) {
        return;
    }
    const rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(at)];
    if (o.mesh != doc.picked_mesh || rv_editor_scene_visibility(doc, o) == rv_editor_visibility::absent) {
        return;
    }
    // A model that does not load has no pieces to show; its error is on the object's edges.
    const std::shared_ptr<const rv_editor_glb_model> glb = rv_editor_parts_model(project, o.mesh);
    if (!glb || glb->pieces.size() != doc.picked_count) {
        return;
    }
    const rv_editor_affine m = rv_editor_scene_world(doc.scene, at);
    for (const size_t number : doc.picked_pieces) {
        // Picks belong to this mesh; a number past its pieces is ignored.
        if (number == 0 || number > glb->pieces.size()) {
            continue;
        }
        for (const uint32_t t : glb->pieces[number - 1]) {
            const rv_pdklib::rv_mppcmod_triangle &tri = glb->model.triangles[t];
            vec3 p[std::size(tri.index)];
            for (size_t k = 0; k < std::size(tri.index); ++k) {
                const rv_pdklib::rv_mppcmod_vertex &vx = glb->model.vertices[tri.index[k]];
                p[k] = rv_editor_affine_point(m, { vx.pos[0], vx.pos[1], vx.pos[2] });
            }
            for (size_t k = 0; k < std::size(tri.index); ++k) {
                rv_editor_line(dl, v, p[k], p[(k + 1) % std::size(tri.index)], color, line_width_picked);
            }
        }
    }
}

} // namespace rv_editor
