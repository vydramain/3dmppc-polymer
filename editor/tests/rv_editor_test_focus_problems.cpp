// A failed or unstarted build brings the Inspector Report forward: the shell function that finds or adds it and gives it
// the keyboard. What it says of the failure is in rv_editor_test_report_strip.cpp.

#include <string>

#include "app/rv_editor_shell.hpp"
#include "layout/rv_editor_tile.hpp"
#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"

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
    RV_EDITOR_CHECK(pane_count(s, rv_editor_pane_kind::inspector_report) == 1);
    const rv_editor_pane_id problems = find_pane(s, rv_editor_pane_kind::inspector_report);
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
        rv_editor_tile_find(shell.ws.layout, find_pane(shell, rv_editor_pane_kind::inspector_report)));
}

// Scene: the Inspector Report is a tab of the Assets tile (S5: every layout starts with it), where the Output is.
RV_EDITOR_TEST(rv_editor_test_focus_problems_scene)
{
    rv_editor_shell shell;
    init_shell(shell, rv_editor_layout_preset::scene);
    RV_EDITOR_CHECK(pane_count(shell, rv_editor_pane_kind::inspector_report) == 1);
    const size_t leaves = leaf_count(shell.ws.layout);
    rv_editor_shell_focus_problems(shell);
    check_focused(shell, leaves);
    const rv_editor_pane_id output = find_pane(shell, rv_editor_pane_kind::output);
    RV_EDITOR_CHECK(rv_editor_tile_find(shell.ws.layout, output) ==
        rv_editor_tile_find(shell.ws.layout, find_pane(shell, rv_editor_pane_kind::inspector_report)));
}

// Debug and Burn (S5): the Inspector Report is a tab of the log tile, Runtime Log and Build Log; it comes to the front there.
RV_EDITOR_TEST(rv_editor_test_focus_problems_debug)
{
    rv_editor_shell shell;
    init_shell(shell, rv_editor_layout_preset::debug);
    RV_EDITOR_CHECK(pane_count(shell, rv_editor_pane_kind::inspector_report) == 1);
    const size_t leaves = leaf_count(shell.ws.layout);
    rv_editor_shell_focus_problems(shell);
    check_focused(shell, leaves);
    RV_EDITOR_CHECK(rv_editor_tile_find(shell.ws.layout, find_pane(shell, rv_editor_pane_kind::runtime_log)) ==
        rv_editor_tile_find(shell.ws.layout, find_pane(shell, rv_editor_pane_kind::inspector_report)));
}

RV_EDITOR_TEST(rv_editor_test_focus_problems_burn)
{
    rv_editor_shell shell;
    init_shell(shell, rv_editor_layout_preset::burn);
    RV_EDITOR_CHECK(pane_count(shell, rv_editor_pane_kind::inspector_report) == 1);
    const size_t leaves = leaf_count(shell.ws.layout);
    rv_editor_shell_focus_problems(shell);
    check_focused(shell, leaves);
    RV_EDITOR_CHECK(rv_editor_tile_find(shell.ws.layout, find_pane(shell, rv_editor_pane_kind::build_log)) ==
        rv_editor_tile_find(shell.ws.layout, find_pane(shell, rv_editor_pane_kind::inspector_report)));
}

// Problems already in a tile away from the Output is activated where it is, not added again.
RV_EDITOR_TEST(rv_editor_test_focus_problems_keeps_existing_tab)
{
    rv_editor_shell shell;
    init_shell(shell, rv_editor_layout_preset::debug);
    rv_editor_shell_show_pane(shell, rv_editor_pane_kind::inspector_report, false);
    const rv_editor_pane_id before = find_pane(shell, rv_editor_pane_kind::inspector_report);
    rv_editor_shell_show_pane(shell, rv_editor_pane_kind::findings, false);
    const size_t leaves = leaf_count(shell.ws.layout);
    rv_editor_shell_focus_problems(shell);
    check_focused(shell, leaves);
    RV_EDITOR_CHECK(find_pane(shell, rv_editor_pane_kind::inspector_report) == before);
}
