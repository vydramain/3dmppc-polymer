// The Scene tile's model tab: a .gltf/.glb drawn textured through its own camera, which the
// mouse and keys move as the Scene viewport's do. Its camera is kept per absolute path here.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "panes/rv_editor_scene_draw.hpp"
#include "panes/rv_editor_scene_glb.hpp"
#include "scene/rv_editor_scene.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// The smallest view in pixels, so the camera always has a frame.
constexpr float model_min_size = 40.0f;

// A model tab's camera and its carry: the movement accumulated but not yet applied (rv_editor_camera_carry).
// One per absolute path, kept while the editor runs. Ponytail ceiling: a closed
// tab's view is not released, and its parsed model stays in the reader's cache (rv_editor_glb_cache)
// until the file's mtime moves.
struct rv_editor_model_view {
    rv_editor_scene_camera cam;
    rv_editor_camera_carry carry;
    // The first show aims the camera at the model's box once; later frames keep the user's view.
    bool framed = false;
};

std::unordered_map<std::filesystem::path, rv_editor_model_view> rv_editor_model_views;

// The box of the corners that fit Q20.12, in scene units; false when no corner does.
bool rv_editor_model_box(const std::vector<rv_editor_tri> &tris, vec3 &lo, vec3 &hi)
{
    lo = { bounding_box_infinity, bounding_box_infinity, bounding_box_infinity };
    hi = { -bounding_box_infinity, -bounding_box_infinity, -bounding_box_infinity };
    bool any = false;
    for (const rv_editor_tri &t : tris) {
        if (!t.q_valid) {
            continue;
        }
        for (int k = 0; k < tri_vertices; ++k) {
            const rv_pdklib::fx_vec3 &q = t.q[k];
            const vec3 p{ static_cast<double>(q.x) / rv_pdklib::fx_one,
                static_cast<double>(q.y) / rv_pdklib::fx_one,
                static_cast<double>(q.z) / rv_pdklib::fx_one };
            for (size_t a = 0; a < p.size(); ++a) {
                lo[a] = std::min(lo[a], p[a]);
                hi[a] = std::max(hi[a], p[a]);
            }
            any = true;
        }
    }
    return any;
}

void rv_editor_model_label(const std::string &text, const rv_editor_theme &theme)
{
    rv_editor_label(text.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
}

} // namespace

void rv_editor_scene_tab_model(SDL_Renderer *renderer, const rv_editor_scene_tab &tab, const rv_editor_theme &theme)
{
    const std::string name = tab.path.filename().string();
    std::vector<rv_editor_tri> tris;
    std::string error;
    // Rebuilt each frame, as the Scene viewport rebuilds its triangles; the reader's cache keeps the model
    // by mtime, so a frame is one walk of its triangles. Ceiling: a heavy model costs every frame.
    const int read_rc = rv_editor_glb_path_triangles(tab.path, rv_pdklib::gte_identity, tris, error);
    if (read_rc != RV_OK) {
        const std::string error_msg =
            rv_editor_text_format("pane_scene_tabs.error_model_load", std::make_format_args(name, error));
        rv_editor_model_label(error_msg, theme);
        return;
    }
    // The reader leaves out triangles outside Q20.12 and names them here; the model still shows.
    if (!error.empty()) {
        const std::string warning_msg =
            rv_editor_text_format("pane_scene_tabs.warning_triangles_dropped", std::make_format_args(error));
        rv_editor_model_label(warning_msg, theme);
    }

    rv_editor_model_view &view = rv_editor_model_views[tab.path];
    vec3 lo{};
    vec3 hi{};
    if (!view.framed && rv_editor_model_box(tris, lo, hi)) {
        view.framed = true;
        // A refused frame leaves the default view; the box is not retried, so the refusal is logged here.
        if (rv_editor_frame_box(view.cam, lo, hi) != RV_OK) {
            RV_LOG_WARN(scene_log_tag, "model '{}': not aimed at its box, the default view stays", tab.path.string());
        }
    }
    // Checked before the view is drawn: a stale saved view is reset here, with one warning.
    if (rv_editor_camera_check(view.cam) != RV_OK) {
        view.carry = rv_editor_camera_carry{};
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size(std::max(model_min_size, avail.x), std::max(model_min_size, avail.y));
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##model_view",
        size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused() || ImGui::IsWindowFocused();
    const ImGuiIO &io = ImGui::GetIO();
    const bool keys_free = focused && !io.WantTextInput && !ImGui::IsAnyItemActive();

    const rv_editor_view v = rv_editor_view_make(view.cam, p0, size);
    if (!v.valid) {
        rv_editor_model_label(rv_editor_text("pane_scene_tabs.error_model_view"), theme);
        return;
    }
    // The navigation's refusal is logged by the camera calls, as in the Scene viewport; the code is dropped.
    (void)rv_editor_camera_navigate(view.cam, view.carry, v, hovered, active, keys_free);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    dl->PushClipRect(p0, p1, true);
    dl->AddRectFilled(p0, p1, rv_editor_col(theme.code_base));
    rv_editor_draw_triangles(dl, v, tris, true, renderer, ImGui::GetID("##model_picture"), p0, size);
    dl->PopClipRect();
}

} // namespace rv_editor
