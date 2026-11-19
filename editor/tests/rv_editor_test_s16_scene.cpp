// S16, R15/R19/R20, AC16/AC21: the Scene pane. The New Scene question is a message box with no ask place; the read-only
// line and the error are a band over the viewport (rv_editor_picture_overlay), not lines that take height from it; the
// viewport takes all the room under the shelf, is the same rectangle with an empty or a long note, and is at least half
// the tile in the Scene layout.

#include <algorithm>
#include <array>
#include <iterator>
#include <cstdio>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "layout/rv_editor_tile.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s16.hpp"
#include "panes/rv_editor_pane_tiled.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s16;

namespace
{

constexpr float roomy_w = 600.0f;
constexpr float roomy_h = 500.0f;
constexpr size_t scene_state = 2;
constexpr size_t read_only_state = 3;
constexpr size_t error_state = 4;
constexpr size_t ask_state = 5;

using vertex = std::array<float, 5>; // x, y, u, v, colour

// Every vertex the last frame drew, sorted.
std::vector<vertex> vertices()
{
    ImGui::Render();
    std::vector<vertex> out;
    const ImDrawData *data = ImGui::GetDrawData();
    for (int i = 0; data != nullptr && i < data->CmdLists.Size; ++i) {
        for (const ImDrawVert &v : data->CmdLists[i]->VtxBuffer) {
            out.push_back({ v.pos.x, v.pos.y, v.uv.x, v.uv.y, static_cast<float>(v.col) });
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// What a state draws over the scene state: the vertices only it has.
struct overlay {
    std::vector<vertex> added;
    const rv_editor_probe_entry *viewport = nullptr;
    rv_editor_probe_record record; // the state's own, so `viewport` stays valid
};

overlay over_scene(fx &f, size_t state)
{
    overlay o;
    f.states[scene_state]();
    settled(f, roomy_w, roomy_h);
    f.draw(roomy_w, roomy_h);
    const std::vector<vertex> base = vertices();
    f.states[state]();
    o.record = settled(f, roomy_w, roomy_h);
    f.draw(roomy_w, roomy_h);
    const std::vector<vertex> now = vertices();
    std::set_difference(now.begin(), now.end(), base.begin(), base.end(), std::back_inserter(o.added));
    o.viewport = find(o.record, "##viewport");
    return o;
}

void check_band(fx &f, size_t state, const char *what)
{
    const overlay o = over_scene(f, state);
    RV_EDITOR_CHECK(o.viewport != nullptr);
    if (o.viewport == nullptr) {
        return;
    }
    // The line is gone as an item of its own: no message line takes height from the viewport.
    RV_EDITOR_CHECK(find(o.record, "##read_only") == nullptr && find(o.record, "##error") == nullptr);
    // The text and its backing are drawn, and all of it inside the picture.
    if (o.added.empty()) {
        std::fprintf(stderr, "  %s: nothing is drawn over the viewport\n", what);
    }
    RV_EDITOR_CHECK(!o.added.empty());
    for (const vertex &v : o.added) {
        if (v[0] < o.viewport->x0 - 1.0f || v[0] > o.viewport->x1 + 1.0f || v[1] < o.viewport->y0 - 1.0f ||
            v[1] > o.viewport->y1 + 1.0f) {
            std::fprintf(stderr,
                "  %s: a vertex at %.1f,%.1f is outside the viewport %s\n",
                what,
                v[0],
                v[1],
                rv_editor_probe_detail::rect(*o.viewport).c_str());
            RV_EDITOR_CHECK(false);
            return;
        }
    }
}

} // namespace

// R15, AC16: the read-only line is a band inside the viewport, and the viewport is the same rectangle without it.
RV_EDITOR_TEST(rv_editor_test_s16_scene_read_only_is_a_band_over_the_viewport)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fx f(gui, rv_editor_pane_kind::scene);
    check_band(f, read_only_state, "read-only");
    f.states[read_only_state]();
    const auto noted = settled(f, roomy_w, roomy_h);
    f.states[scene_state]();
    const auto none = settled(f, roomy_w, roomy_h);
    const std::string moved = s13::same_rect(find(none, "##viewport"), find(noted, "##viewport"), "viewport");
    report("scene", moved);
    RV_EDITOR_CHECK(moved.empty());
}

// R15, AC16: the same for an error, empty or very long.
RV_EDITOR_TEST(rv_editor_test_s16_scene_error_is_a_band_over_the_viewport)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fx f(gui, rv_editor_pane_kind::scene);
    check_band(f, error_state, "error");
    f.states[error_state]();
    const auto long_note = settled(f, roomy_w, roomy_h);
    f.states[scene_state]();
    const auto none = settled(f, roomy_w, roomy_h);
    const std::string moved = s13::same_rect(find(none, "##viewport"), find(long_note, "##viewport"), "viewport");
    report("scene", moved);
    RV_EDITOR_CHECK(moved.empty());
}

// R20: no ask place anywhere in the Scene, in any state, and the New Scene question is a message box that holds the name
// field and the Create and Cancel buttons.
RV_EDITOR_TEST(rv_editor_test_s16_new_scene_question_is_a_message_box_with_no_reserve)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fx f(gui, rv_editor_pane_kind::scene);
    for (size_t i = 0; i < f.states.size(); ++i) {
        f.states[i]();
        const auto r = settled(f, roomy_w, roomy_h);
        if (has_ask_place(r)) {
            std::fprintf(stderr, "  scene state %zu draws an ask place\n", i);
        }
        RV_EDITOR_CHECK(!has_ask_place(r));
        if (i != ask_state) {
            continue;
        }
        RV_EDITOR_CHECK(message_box(r) != nullptr);
        const auto *name = find(r, "##new_scene_name");
        RV_EDITOR_CHECK(name != nullptr);
        RV_EDITOR_CHECK(find(r, rv_editor_text("shell_menu_scene.create_button")) != nullptr);
        RV_EDITOR_CHECK(find(r, rv_editor_text("shell_menu_scene.cancel_button")) != nullptr);
    }
}

// R20, AC21: with the question closed nothing is reserved: what is above and below the viewport is the shelf, the
// message box, the wheels and the spacing, so a 6-line ask place that stays empty would not fit.
RV_EDITOR_TEST(rv_editor_test_s16_viewport_takes_everything_under_the_shelf)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fx f(gui, rv_editor_pane_kind::scene);
    f.states[scene_state]();
    const auto r = settled(f, roomy_w, roomy_h);
    const auto *viewport = find(r, "##viewport");
    RV_EDITOR_CHECK(viewport != nullptr);
    if (viewport == nullptr) {
        return;
    }
    float allowed = 0.0f;
    f.gui.frame([&] {
        const ImGuiStyle &style = ImGui::GetStyle();
        const float frame = ImGui::GetFrameHeight();
        // the shelf, the message box, the wheel under the viewport, the window's padding and a few spacings
        allowed = rv_editor_tiled_shelf_block() + rv_editor_tiled_box_block() + frame + 2.0f * style.WindowPadding.y +
            4.0f * style.ItemSpacing.y;
    });
    const float spare = roomy_h - (viewport->y1 - viewport->y0);
    if (spare > allowed) {
        std::fprintf(stderr,
            "  the viewport is %.0f px high in a tile of %.0f: %.0f px is not the viewport, only %.0f may be\n",
            viewport->y1 - viewport->y0,
            roomy_h,
            spare,
            allowed);
    }
    RV_EDITOR_CHECK(spare <= allowed);
}

// AC21: in the Scene layout at 1280x720 the viewport is at least half the height of its tile.
RV_EDITOR_TEST(rv_editor_test_s16_viewport_is_half_the_tile_in_the_scene_layout)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fx f(gui, rv_editor_pane_kind::scene);
    rv_editor_shell shell;
    rv_editor_layout_preset_make(rv_editor_layout_preset::scene, shell.ws.panes, shell.ws.layout);
    std::vector<rv_editor_size> pane_min(shell.ws.panes.panes.size(), rv_editor_size{ 0, 0 });
    for (size_t i = 0; i < pane_min.size(); ++i) {
        rv_editor_shell_set_minimum(shell, static_cast<rv_editor_pane_id>(i), shell.ws.panes.panes[i].kind, { 0, 0 });
        const auto it = shell.ws.minimums.find(static_cast<rv_editor_pane_id>(i));
        if (it != shell.ws.minimums.end()) {
            pane_min[i] = it->second;
        }
    }
    // Stand-ins for what the view measures: bar, leaf chrome, the tab row.
    const rv_editor_tile_metrics m{ 4, { 12, 36 }, 20 };
    rv_editor_rect tile{ 0, 0, 0, 0 };
    for (const auto &place : rv_editor_layout_place(shell.ws.layout, rv_editor_rect{ 0, 0, 1280, 720 }, m, pane_min)) {
        const auto &node = shell.ws.layout.nodes[place.node];
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id id : node.leaf.tabs) {
            if (shell.ws.panes.panes[id].kind == rv_editor_pane_kind::scene) {
                tile = place.rect;
            }
        }
    }
    RV_EDITOR_CHECK(tile.w > 0 && tile.h > 0);
    if (tile.w <= 0 || tile.h <= 0) {
        return;
    }
    f.states[scene_state]();
    const auto r = settled(f, static_cast<float>(tile.w - m.chrome.w), static_cast<float>(tile.h - m.chrome.h - m.tabs));
    const auto *viewport = find(r, "##viewport");
    RV_EDITOR_CHECK(viewport != nullptr);
    if (viewport == nullptr) {
        return;
    }
    const float height = viewport->y1 - viewport->y0;
    if (height * 2.0f < static_cast<float>(tile.h)) {
        std::fprintf(stderr, "  the viewport is %.0f px high in a Scene tile %dx%d\n", height, tile.w, tile.h);
    }
    RV_EDITOR_CHECK(height * 2.0f >= static_cast<float>(tile.h));
}
