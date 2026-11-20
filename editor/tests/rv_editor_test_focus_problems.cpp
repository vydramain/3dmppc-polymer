// A failed or unstarted build brings Problems forward: the shell function that finds or adds it and gives it the
// keyboard, and the ask area Problems draws at its top while app.build_alert is set.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "layout/rv_editor_tile.hpp"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;

namespace
{

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

size_t leaf_count(const rv_editor_layout &l)
{
    size_t n = 0;
    for (const rv_editor_tile_node &node : l.nodes) {
        n += node.kind == rv_editor_tile_kind::leaf ? 1 : 0;
    }
    return n;
}

size_t pane_count(const rv_editor_shell &s, rv_editor_pane_kind kind)
{
    size_t n = 0;
    for (const rv_editor_tile_node &node : s.ws.layout.nodes) {
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id id : node.leaf.tabs) {
            n += s.ws.panes.panes[id].kind == kind ? 1 : 0;
        }
    }
    return n;
}

rv_editor_pane_id find_pane(const rv_editor_shell &s, rv_editor_pane_kind kind)
{
    for (const rv_editor_tile_node &node : s.ws.layout.nodes) {
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id id : node.leaf.tabs) {
            if (s.ws.panes.panes[id].kind == kind) {
                return id;
            }
        }
    }
    return rv_editor_tile_none;
}

// Problems is in one leaf, is that leaf's active tab, the leaf has the keyboard, and no tile appeared.
void check_focused(const rv_editor_shell &s, size_t leaves_before)
{
    RV_EDITOR_CHECK(pane_count(s, rv_editor_pane_kind::problems) == 1);
    const rv_editor_pane_id problems = find_pane(s, rv_editor_pane_kind::problems);
    if (problems == rv_editor_tile_none) {
        return;
    }
    const uint32_t leaf = rv_editor_tile_find(s.ws.layout, problems);
    RV_EDITOR_CHECK(leaf != rv_editor_tile_none);
    if (leaf == rv_editor_tile_none) {
        return;
    }
    const rv_editor_tile_leaf &l = s.ws.layout.nodes[leaf].leaf;
    RV_EDITOR_CHECK(l.active < l.tabs.size() && l.tabs[l.active] == problems);
    RV_EDITOR_CHECK(s.ws.focused_leaf == leaf);
    RV_EDITOR_CHECK(leaf_count(s.ws.layout) == leaves_before);
}

void init_shell(rv_editor_shell &shell, rv_editor_layout_preset preset)
{
    rv_editor_layout_preset_make(preset, shell.ws.panes, shell.ws.layout);
    shell.active = preset;
}

} // namespace

// Code: Problems is a tab behind the Output's; it comes to the front in the same tile.
RV_EDITOR_TEST(rv_editor_test_focus_problems_code)
{
    rv_editor_shell shell;
    init_shell(shell, rv_editor_layout_preset::code);
    const size_t leaves = leaf_count(shell.ws.layout);
    rv_editor_shell_focus_problems(shell);
    check_focused(shell, leaves);
    const rv_editor_pane_id output = find_pane(shell, rv_editor_pane_kind::output);
    RV_EDITOR_CHECK(rv_editor_tile_find(shell.ws.layout, output) ==
        rv_editor_tile_find(shell.ws.layout, find_pane(shell, rv_editor_pane_kind::problems)));
}

// Scene: no Problems; it is added as a tab of the Assets tile, where the Output is.
RV_EDITOR_TEST(rv_editor_test_focus_problems_scene)
{
    rv_editor_shell shell;
    init_shell(shell, rv_editor_layout_preset::scene);
    RV_EDITOR_CHECK(pane_count(shell, rv_editor_pane_kind::problems) == 0);
    const size_t leaves = leaf_count(shell.ws.layout);
    rv_editor_shell_focus_problems(shell);
    check_focused(shell, leaves);
    const rv_editor_pane_id output = find_pane(shell, rv_editor_pane_kind::output);
    RV_EDITOR_CHECK(rv_editor_tile_find(shell.ws.layout, output) ==
        rv_editor_tile_find(shell.ws.layout, find_pane(shell, rv_editor_pane_kind::problems)));
}

// Debug and Burn have no Output: Problems is added where rv_editor_shell_show puts a pane, no new tile.
RV_EDITOR_TEST(rv_editor_test_focus_problems_debug)
{
    rv_editor_shell shell;
    init_shell(shell, rv_editor_layout_preset::debug);
    RV_EDITOR_CHECK(pane_count(shell, rv_editor_pane_kind::problems) == 0);
    const size_t leaves = leaf_count(shell.ws.layout);
    rv_editor_shell_focus_problems(shell);
    check_focused(shell, leaves);
}

RV_EDITOR_TEST(rv_editor_test_focus_problems_burn)
{
    rv_editor_shell shell;
    init_shell(shell, rv_editor_layout_preset::burn);
    RV_EDITOR_CHECK(pane_count(shell, rv_editor_pane_kind::problems) == 0);
    const size_t leaves = leaf_count(shell.ws.layout);
    rv_editor_shell_focus_problems(shell);
    check_focused(shell, leaves);
}

// Problems already in a tile away from the Output is activated where it is, not added again.
RV_EDITOR_TEST(rv_editor_test_focus_problems_keeps_existing_tab)
{
    rv_editor_shell shell;
    init_shell(shell, rv_editor_layout_preset::debug);
    rv_editor_shell_show_pane(shell, rv_editor_pane_kind::problems, false);
    const rv_editor_pane_id before = find_pane(shell, rv_editor_pane_kind::problems);
    rv_editor_shell_show_pane(shell, rv_editor_pane_kind::findings, false);
    const size_t leaves = leaf_count(shell.ws.layout);
    rv_editor_shell_focus_problems(shell);
    check_focused(shell, leaves);
    RV_EDITOR_CHECK(find_pane(shell, rv_editor_pane_kind::problems) == before);
}

namespace
{

// The build alert's ask place has a constant id, so its child window is there whether the alert is open or closed.
constexpr const char *build_alert_place_id = "##build_alert";

// The ask child's window inside the test window, found by the name ImGui gives it under the place's id.
ImGuiWindow *ask_window()
{
    ImGui::PushID(build_alert_place_id);
    const ImGuiID id = ImGui::GetID("##ask");
    ImGui::PopID();
    char name[128];
    std::snprintf(name, sizeof(name), "test/##ask_%08X", id);
    return ImGui::FindWindowByName(name);
}

void open_app(rv_editor_app &app)
{
    app.project.open = true;
}

// The index count of the draw commands the window and its child windows (the reason's among them) submitted this frame.
// Commands only: a closed place's content goes to a dropped channel, its vertices may stay in the buffer.
int drawn_indices(const ImGuiWindow *w)
{
    int n = 0;
    for (const ImGuiWindow *x : ImGui::GetCurrentContext()->Windows) {
        for (const ImGuiWindow *p = x; p != nullptr; p = p->ParentWindow) {
            if (p == w) {
                for (const ImDrawCmd &cmd : x->DrawList->CmdBuffer) {
                    n += static_cast<int>(cmd.ElemCount);
                }
                break;
            }
        }
    }
    return n;
}

// What one frame of Problems left in the ask place.
struct ask_draw {
    bool exists = false; // the place's window was submitted, open or closed
    int drawn = 0;       // what the place draws this frame: nothing while closed
    ImRect rect;
};

ask_draw draw_problems(rv_editor_test::rv_editor_test_imgui &gui, rv_editor_app &app)
{
    const rv_editor_theme theme = rv_editor_theme_olive;
    ask_draw out;
    gui.frame([&]() {
        rv_editor_pane_problems(app, theme);
        if (ImGuiWindow *w = ask_window()) {
            out.exists = true;
            out.drawn = drawn_indices(w);
            out.rect = w->Rect();
        }
    });
    return out;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_problems_ask_not_started)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    rv_editor_app app;
    open_app(app);
    app.build_alert = { rv_editor_build_alert_kind::not_started, "no toolchain" };
    const ask_draw ask = draw_problems(gui, app);
    RV_EDITOR_CHECK(ask.exists && ask.drawn > 0);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started); // drawing alone does not clear
}

RV_EDITOR_TEST(rv_editor_test_problems_ask_failed)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    rv_editor_app app;
    open_app(app);
    app.build_alert = { rv_editor_build_alert_kind::failed, "compile error" };
    const ask_draw failed = draw_problems(gui, app);
    RV_EDITOR_CHECK(failed.exists && failed.drawn > 0);
    // The title follows the kind: the same reason under a refused build draws differently.
    app.build_alert.kind = rv_editor_build_alert_kind::not_started;
    RV_EDITOR_CHECK(draw_problems(gui, app).drawn != failed.drawn);
}

RV_EDITOR_TEST(rv_editor_test_problems_no_ask_without_alert)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    rv_editor_app app;
    open_app(app);
    const ask_draw ask = draw_problems(gui, app);
    RV_EDITOR_CHECK(ask.exists);     // the place is always there
    RV_EDITOR_CHECK(ask.drawn == 0); // closed: nothing is drawn
}

// The reason is drawn: a longer one puts more glyphs in the ask.
RV_EDITOR_TEST(rv_editor_test_problems_ask_draws_reason)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    rv_editor_app app;
    open_app(app);
    app.build_alert = { rv_editor_build_alert_kind::failed, "ab" };
    const int shorter = draw_problems(gui, app).drawn;
    app.build_alert.reason = "ab cd ef gh ij kl";
    const int longer = draw_problems(gui, app).drawn;
    RV_EDITOR_CHECK(shorter > 0);
    RV_EDITOR_CHECK(longer > shorter);
}

// OK clears the alert: once the ask has its size, a click somewhere on its rectangle lands on the button, and the
// place is closed from the next frame on: it draws nothing.
RV_EDITOR_TEST(rv_editor_test_problems_ask_ok_clears_alert)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    rv_editor_app app;
    open_app(app);
    app.build_alert = { rv_editor_build_alert_kind::failed, "compile error" };
    ask_draw ask;
    for (int i = 0; i < 4; ++i) {
        ask = draw_problems(gui, app);
        RV_EDITOR_CHECK(ask.drawn > 0);
    }
    const ImRect r = ask.rect;
    ImGuiIO &io = ImGui::GetIO();
    for (float y = r.Min.y + 2; y < r.Max.y && app.build_alert.kind != rv_editor_build_alert_kind::none; y += 4) {
        for (float x = r.Min.x + 2; x < r.Max.x && app.build_alert.kind != rv_editor_build_alert_kind::none; x += 6) {
            io.AddMousePosEvent(x, y);
            draw_problems(gui, app);
            io.AddMouseButtonEvent(0, true);
            draw_problems(gui, app);
            io.AddMouseButtonEvent(0, false);
            draw_problems(gui, app);
        }
    }
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::none);
    const ask_draw closed = draw_problems(gui, app);
    RV_EDITOR_CHECK(closed.exists);
    RV_EDITOR_CHECK(closed.drawn == 0);
}
