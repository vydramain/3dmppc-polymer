// The Scene viewport's Parts mode: a click picks one piece of a model (its triangles joined by
// shared vertices), and the selected object's picked pieces are drawn over its edges.

#include "panes/rv_editor_scene_draw.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>

#include "imgui.h"

#include "app/rv_editor_app_parts.hpp"
#include "panes/rv_editor_zbuf.hpp"
#include "pdk/cv/rv_primitives.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_project.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "panes/rv_editor_scene_glb.hpp"
#include "project/rv_editor_project.hpp"
#include "scene/rv_editor_scene_edit.hpp"

namespace rv_editor
{

namespace
{

// A picked piece's edges are drawn this wide, over the object's edges.
constexpr float line_width_picked = 2.0f;

// A sample point in the zbuf's doubled units (rv_editor_zbuf.hpp).
struct rv_editor_parts_point {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

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

// Triangles counted as refused by rv_editor_parts_nearest and rv_editor_parts_each_picked; logged once per process.
void rv_editor_parts_report(std::size_t refused)
{
    static bool reported = false;
    if (refused == 0 || reported) {
        return;
    }
    reported = true;
    RV_LOG_WARN(scene_log_tag,
        "{} model triangles are not picked or drawn: a corner outside Q20.12, int32 or the z-buffer's corner limit, "
        "or a depth key that cannot be built, or a pick sample past the z-buffer's limit",
        refused);
}

// Whether the sample p is covered by the screen triangle s, by the zbuf's rule (rv_editor_zbuf_covers).
bool rv_editor_parts_covers(const std::array<rv_pdklib::rv_gte_screen, tri_vertices> &s, rv_editor_parts_point p)
{
    std::int32_t x[RV_EDITOR_ZBUF_CORNERS] = {};
    std::int32_t y[RV_EDITOR_ZBUF_CORNERS] = {};
    for (std::size_t k = 0; k < RV_EDITOR_ZBUF_CORNERS; ++k) {
        x[k] = s[k].x;
        y[k] = s[k].y;
    }
    return rv_editor_zbuf_covers(x, y, p.x, p.y);
}

// Whether the raster drops the screen triangle under `cull` (rv_editor_piece_culled, rv_editor_scene_raster.cpp): a zero
// area is dropped in every mode, the rest by the mode.
bool rv_editor_parts_culled(const std::array<rv_pdklib::rv_gte_screen, tri_vertices> &s, std::uint32_t cull)
{
    const bool zero_area = rv_pdklib::rv_gte_culled(s[0], s[1], s[2], rv_pdklib::gte_cull_screen_cw) &&
        rv_pdklib::rv_gte_culled(s[0], s[1], s[2], rv_pdklib::gte_cull_screen_ccw);
    return zero_area || rv_pdklib::rv_gte_culled(s[0], s[1], s[2], cull);
}

// A screen corner past the zbuf's coordinate limit: the zbuf refuses such a corner, so the triangle is refused here too.
bool rv_editor_parts_beyond(rv_pdklib::rv_gte_screen s)
{
    return std::abs(static_cast<std::int64_t>(s.x)) > RV_EDITOR_ZBUF_COORD_LIMIT ||
        std::abs(static_cast<std::int64_t>(s.y)) > RV_EDITOR_ZBUF_COORD_LIMIT;
}

// A pointer offset from the view centre, in pixels, that keeps its doubled sample within RV_EDITOR_ZBUF_COORD_LIMIT.
// The bound is half the limit less one, so the floor, doubled and shifted to the centre, stays inside it. Checked
// before the int64 cast: a non-finite or huge float has no defined cast.
bool rv_editor_parts_offset_in_range(float offset)
{
    constexpr double half_limit = static_cast<double>(RV_EDITOR_ZBUF_COORD_LIMIT) / 2 - 1;
    return std::isfinite(offset) && std::abs(static_cast<double>(offset)) <= half_limit;
}

// The triangle's ordering key, as rv_editor_raster_piece builds it: the console's depth key over the view z of its corners.
int rv_editor_parts_depth(const std::array<rv_pdklib::fx_vec3, tri_vertices> &view, std::int32_t &key)
{
    const rv_pdklib::fx z[tri_vertices] = { view[0].z, view[1].z, view[2].z };
    const rv_pdklib::rv_gte_depth_range range = { rv_editor_near_fx, rv_editor_camera_far_fx, RV_DEPTH_MIN, RV_DEPTH_MAX };
    return rv_pdklib::rv_gte_depth_key(z, tri_vertices, range, key);
}

// A triangle's Q20.12 corners through the object's world chain. RV_ERR_INVAL when a corner leaves int32.
int rv_editor_parts_world(const rv_editor_glb_model &glb,
    const rv_pdklib::rv_gte_matrix &world,
    const rv_pdklib::rv_mppcmod_triangle &tri,
    std::array<rv_pdklib::fx_vec3, tri_vertices> &q)
{
    for (std::size_t k = 0; k < tri_vertices; ++k) {
        if (rv_editor_glb_vertex_in_world(glb.quantized, world, tri.index[k], q[k]) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// The corners' view-space Q20.12 and their pixels from the view centre, projected as the overlay's edges are
// (rv_gte_project with the camera's focal length). RV_ERR_NOENT when a corner is behind the near plane: the triangle is
// not picked, as before; the raster draws its visible part, so a click there misses it. RV_ERR_INVAL when a value leaves
// int32 or a pixel leaves RV_EDITOR_ZBUF_COORD_LIMIT: the triangle is refused.
int rv_editor_parts_front(const rv_editor_view &v,
    const std::array<rv_pdklib::fx_vec3, tri_vertices> &q,
    std::array<rv_pdklib::fx_vec3, tri_vertices> &view,
    std::array<rv_pdklib::rv_gte_screen, tri_vertices> &screen)
{
    for (std::size_t k = 0; k < tri_vertices; ++k) {
        const std::int32_t in[3] = { q[k].x, q[k].y, q[k].z };
        std::int32_t out[3] = {};
        if (rv_pdklib::rv_gte_point(v.camera.view, in, out) != RV_OK) {
            return RV_ERR_INVAL;
        }
        view[k] = { out[0], out[1], out[2] };
        if (view[k].z < rv_editor_near_fx) {
            return RV_ERR_NOENT;
        }
        if (rv_pdklib::rv_gte_project(view[k], { v.camera.h, 0, 0 }, screen[k]) != RV_OK) {
            return RV_ERR_INVAL;
        }
        if (rv_editor_parts_beyond(screen[k])) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// The triangle chosen at `sample` by the raster's coverage, cull and depth-key rules: covering it (rv_editor_parts_covers),
// not culled under `cull` (rv_editor_parts_culled), with the largest depth key; a tie keeps the later one, as the raster
// draws it. An alpha-0 texel is not drawn by the raster but still picks. Returns its index and sets `key`; -1 for none.
// `refused` counts the triangles left out for a corner outside Q20.12, int32 or the zbuf's limit, or a key that cannot
// be built.
int rv_editor_parts_nearest(const rv_editor_glb_model &glb,
    const rv_pdklib::rv_gte_matrix &world,
    const rv_editor_view &v,
    rv_editor_parts_point sample,
    std::uint32_t cull,
    std::int32_t &key,
    std::size_t &refused)
{
    int best = -1;
    for (std::size_t t = 0; t < glb.quantized.triangles.size(); ++t) {
        const rv_pdklib::rv_mppcmod_triangle &tri = glb.quantized.triangles[t];
        std::array<rv_pdklib::fx_vec3, tri_vertices> q{};
        std::array<rv_pdklib::fx_vec3, tri_vertices> view{};
        std::array<rv_pdklib::rv_gte_screen, tri_vertices> screen{};
        std::int32_t tri_key = 0;
        if (rv_editor_parts_world(glb, world, tri, q) != RV_OK) {
            ++refused;
            continue;
        }
        const int rc = rv_editor_parts_front(v, q, view, screen);
        if (rc == RV_ERR_INVAL) {
            ++refused;
            continue;
        }
        if (rc != RV_OK || rv_editor_parts_culled(screen, cull) || !rv_editor_parts_covers(screen, sample)) {
            continue;
        }
        if (rv_editor_parts_depth(view, tri_key) != RV_OK) {
            ++refused;
            continue;
        }
        if (best < 0 || tri_key >= key) {
            best = static_cast<int>(t);
            key = tri_key;
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

static_assert(std::tuple_size_v<decltype(rv_pdklib::rv_mppcmod_triangle::index)> == tri_vertices);

// Calls fn with the Q20.12 world corners of each triangle of the target's picked pieces; none without a model.
template <typename Fn>
void rv_editor_parts_each_picked(const rv_editor_scene_doc &doc, const rv_editor_parts_target &target, Fn &&fn)
{
    if (!target.model) {
        return;
    }
    const rv_editor_glb_model &glb = *target.model;
    rv_pdklib::rv_gte_matrix world{};
    if (rv_editor_scene_world_gte(doc.scene, target.owner, world) != RV_OK) {
        // Once per process: the owner's chain is refused (a bad index, an overflow or a parent cycle).
        static bool reported = false;
        if (!reported) {
            reported = true;
            RV_LOG_WARN(scene_log_tag, "the picked object's world chain is outside Q20.12: its pieces are not drawn");
        }
        return;
    }
    std::size_t refused = 0;
    for (const size_t number : doc.picked_pieces) {
        // Picks belong to this mesh; a number past its pieces is ignored.
        if (number == 0 || number > glb.pieces.size()) {
            continue;
        }
        for (const uint32_t t : glb.pieces[number - 1]) {
            std::array<rv_pdklib::fx_vec3, tri_vertices> q{};
            if (rv_editor_parts_world(glb, world, glb.quantized.triangles[t], q) != RV_OK) {
                ++refused;
                continue;
            }
            fn(q);
        }
    }
    rv_editor_parts_report(refused);
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
    // The sample is the centre of the pointer's pixel, in doubled units; the pixel is the floor of the pointer's offset
    // from v.center. The overlay's pixel is one ImGui point, while the raster's buffer has lround(size * framebuffer
    // scale) pixels with an integer centre: they agree at scale 1 and differ by the scale otherwise (D3, D4).
    // An offset outside the zbuf's sample limit (rv_editor_zbuf_covers) refuses every triangle of the model.
    const float off_x = at.x - v.center.x;
    const float off_y = at.y - v.center.y;
    const bool sample_in_range = rv_editor_parts_offset_in_range(off_x) && rv_editor_parts_offset_in_range(off_y);
    rv_editor_parts_point sample{};
    if (sample_in_range) {
        sample = { RV_EDITOR_ZBUF_DOUBLING * static_cast<std::int64_t>(std::floor(off_x)) + RV_EDITOR_ZBUF_CENTRE_OFFSET,
            RV_EDITOR_ZBUF_DOUBLING * static_cast<std::int64_t>(std::floor(off_y)) + RV_EDITOR_ZBUF_CENTRE_OFFSET };
    }
    std::shared_ptr<const rv_editor_glb_model> best_glb;
    size_t best_object = 0;
    int best_tri = -1;
    std::int32_t best_key = 0;
    std::size_t refused = 0;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const rv_editor_scene_object &o = scene.objects[i];
        if (rv_editor_object_get_kind(o) != kind_mesh || rv_editor_scene_visibility(doc, o) == rv_editor_visibility::absent) {
            continue;
        }
        const std::shared_ptr<const rv_editor_glb_model> glb = rv_editor_parts_model(project, rv_editor_object_get_mesh(o));
        if (!glb) {
            continue;
        }
        if (!sample_in_range) {
            refused += glb->quantized.triangles.size();
            continue;
        }
        rv_pdklib::rv_gte_matrix world{};
        if (rv_editor_scene_world_gte(scene, static_cast<int>(i), world) != RV_OK) {
            ++refused;
            continue;
        }
        // The object's cull mode as the raster gives it (rv_editor_cull_mode): back faces unless two_sided.
        const std::uint32_t cull =
            rv_editor_scene_has_component(o, component_two_sided) ? rv_pdklib::gte_cull_none : rv_pdklib::gte_cull_screen_cw;
        std::int32_t key = 0;
        const int tri = rv_editor_parts_nearest(*glb, world, v, sample, cull, key, refused);
        // Objects are drawn in scene order: a tie keeps the later one, as the raster draws it.
        if (tri >= 0 && (best_tri < 0 || key >= best_key)) {
            best_glb = glb;
            best_object = i;
            best_tri = tri;
            best_key = key;
        }
    }
    rv_editor_parts_report(refused);
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
    doc.picked_mesh = rv_editor_object_get_mesh(o);
    doc.picked_count = glb->pieces.size();
    if (doc.picked_pieces.erase(number) == 0) {
        doc.picked_pieces.insert(number);
    }
}

rv_editor_parts_target rv_editor_parts_resolve(const rv_editor_scene_doc &doc, const rv_editor_project &project)
{
    // Nothing picked: nothing to draw.
    if (doc.picked_pieces.empty()) {
        return {};
    }
    // The picks belong to another object: nothing to draw.
    if (doc.picked_owner != doc.selected) {
        return {};
    }
    const int at = rv_editor_scene_find(doc.scene, doc.picked_owner);
    if (at < 0) {
        return {};
    }
    const rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(at)];
    const std::string mesh = rv_editor_object_get_mesh(o);
    if (mesh != doc.picked_mesh) {
        return {};
    }
    if (rv_editor_scene_visibility(doc, o) == rv_editor_visibility::absent) {
        return {};
    }
    // A model that does not load has no pieces to show; its error is on the object's edges.
    const std::shared_ptr<const rv_editor_glb_model> glb = rv_editor_parts_model(project, mesh);
    if (!glb) {
        return {};
    }
    if (glb->pieces.size() != doc.picked_count) {
        return {};
    }
    return { at, glb };
}

void rv_editor_parts_highlight(ImDrawList *dl,
    const rv_editor_view &v,
    const rv_editor_scene_doc &doc,
    const rv_editor_parts_target &target,
    ImU32 color)
{
    rv_editor_parts_each_picked(doc, target, [&](const std::array<rv_pdklib::fx_vec3, tri_vertices> &q) {
        for (std::size_t k = 0; k < tri_vertices; ++k) {
            rv_editor_line_fx(dl, v, q[k], q[(k + 1) % tri_vertices], color, line_width_picked);
        }
    });
}

bool rv_editor_parts_bounds(const rv_editor_scene_doc &doc, const rv_editor_parts_target &target, vec3 &lo, vec3 &hi)
{
    std::array<rv_pdklib::fx, tri_vertices> low{};
    std::array<rv_pdklib::fx, tri_vertices> high{};
    low.fill(std::numeric_limits<rv_pdklib::fx>::max());
    high.fill(std::numeric_limits<rv_pdklib::fx>::min());
    bool any = false;
    rv_editor_parts_each_picked(doc, target, [&](const std::array<rv_pdklib::fx_vec3, tri_vertices> &q) {
        for (const rv_pdklib::fx_vec3 &c : q) {
            const rv_pdklib::fx xyz[tri_vertices] = { c.x, c.y, c.z };
            for (std::size_t k = 0; k < tri_vertices; ++k) {
                low[k] = std::min(low[k], xyz[k]);
                high[k] = std::max(high[k], xyz[k]);
            }
        }
        any = true;
    });
    if (!any) {
        return false;
    }
    // The box is exact in Q20.12 until here; this is its one conversion to the viewport's units.
    for (std::size_t k = 0; k < tri_vertices; ++k) {
        lo[k] = rv_editor_camera_units_of(low[k]);
        hi[k] = rv_editor_camera_units_of(high[k]);
    }
    return true;
}

} // namespace rv_editor
