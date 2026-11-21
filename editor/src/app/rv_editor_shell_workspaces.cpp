// The layouts Code, Scene, Debug and Burn: choosing one, resetting it, their files.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <system_error>

#include "panes/rv_editor_pane_controls.hpp"
#include "panes/rv_editor_release_facts.hpp"
#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

size_t rv_editor_workspace_slot(rv_editor_layout_preset preset)
{
    for (size_t i = 0; i < std::size(rv_editor_workspaces); ++i) {
        if (rv_editor_workspaces[i] == preset) {
            return i;
        }
    }
    return 0;
}

// "code", "scene", "debug", "burn": the view file's names and the layout files' suffixes.
const char *rv_editor_workspace_key_of(rv_editor_layout_preset preset)
{
    switch (preset) {
    case rv_editor_layout_preset::scene:
        return "scene";
    case rv_editor_layout_preset::debug:
        return "debug";
    case rv_editor_layout_preset::burn:
        return "burn";
    default:
        return "code";
    }
}

std::filesystem::path rv_editor_workspace_file(const std::filesystem::path &path, rv_editor_layout_preset preset)
{
    return path.parent_path() / (path.filename().string() + "-" + rv_editor_workspace_key_of(preset));
}

// Where a layout was kept before it had a file of its own: the single layout
// file for Code, the Test and Release files for Debug and Burn. Read only when
// the layout's own file is missing; never written.
std::filesystem::path rv_editor_workspace_old_file(const std::filesystem::path &path, rv_editor_layout_preset preset)
{
    switch (preset) {
    case rv_editor_layout_preset::code:
        return path;
    case rv_editor_layout_preset::debug:
        return path.parent_path() / (path.filename().string() + "-test");
    case rv_editor_layout_preset::burn:
        return path.parent_path() / (path.filename().string() + "-release");
    default:
        return {};
    }
}

// `preset`'s starting tree, its panes joined to `panes`.
rv_editor_layout rv_editor_workspace_start(rv_editor_pane_registry &panes, rv_editor_layout_preset preset)
{
    rv_editor_pane_registry own;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(preset, own, layout);
    rv_editor_pane_adopt(panes, own, layout);
    return layout;
}

// A leaf of controls or the Toolchest only: a strip as tall as its buttons, never
// stretched for the Game.
bool rv_editor_strip_leaf(const rv_editor_workspace &ws, uint32_t node)
{
    const rv_editor_tile_node &n = ws.layout.nodes[node];
    if (n.kind != rv_editor_tile_kind::leaf || n.leaf.tabs.empty()) {
        return false;
    }
    for (const rv_editor_pane_id pane : n.leaf.tabs) {
        const rv_editor_pane_kind kind = ws.panes.panes[pane].kind;
        if (kind != rv_editor_pane_kind::controls && kind != rv_editor_pane_kind::release_controls &&
            kind != rv_editor_pane_kind::toolchest) {
            return false;
        }
    }
    return true;
}

// The transport strips (Runtime and Release Controls): their height follows the width they are drawn at.
bool rv_editor_transport_kind(rv_editor_pane_kind kind)
{
    return kind == rv_editor_pane_kind::controls || kind == rv_editor_pane_kind::release_controls;
}

bool rv_editor_transport_leaf(const rv_editor_workspace &ws, uint32_t node)
{
    const rv_editor_tile_node &n = ws.layout.nodes[node];
    if (n.kind != rv_editor_tile_kind::leaf || n.leaf.tabs.empty()) {
        return false;
    }
    return std::all_of(n.leaf.tabs.begin(), n.leaf.tabs.end(), [&](rv_editor_pane_id pane) {
        return rv_editor_transport_kind(ws.panes.panes[pane].kind);
    });
}

// `node`'s size along `axis`, the largest of its tabs' minimums (ws.minimums): what a strip leaf needs and nothing more.
// A transport strip's height is the one of its body at the width it was drawn at, plus the leaf's chrome (known once a
// frame has drawn the tree, which is also when ImGui can measure).
int32_t rv_editor_strip_min_along(const rv_editor_workspace &ws, uint32_t node, rv_editor_axis axis)
{
    rv_editor_size need{ 0, 0 };
    for (const rv_editor_pane_id pane : ws.layout.nodes[node].leaf.tabs) {
        const auto it = ws.minimums.find(pane);
        if (it != ws.minimums.end()) {
            need.w = std::max(need.w, it->second.w);
            need.h = std::max(need.h, it->second.h);
        }
        const rv_editor_pane_kind kind = ws.panes.panes[pane].kind;
        if (axis == rv_editor_axis::y && rv_editor_transport_kind(kind) && ws.body_chrome.w > 0 && node < ws.rects.size() &&
            ws.rects[node].w > ws.body_chrome.w) {
            const float body = static_cast<float>(ws.rects[node].w - ws.body_chrome.w);
            const float height =
                kind == rv_editor_pane_kind::controls ? rv_editor_transport_height(body) : rv_editor_release_height(body);
            need.h = std::max(need.h, static_cast<int32_t>(std::ceil(height)) + ws.body_chrome.h);
        }
    }
    return axis == rv_editor_axis::x ? need.w : need.h;
}

// A split holding only strip leaves side by side: not a strip itself, but it
// needs no more than the tallest (or widest) of the strips it holds.
bool rv_editor_strip_row(const rv_editor_workspace &ws, uint32_t node)
{
    const rv_editor_tile_node &n = ws.layout.nodes[node];
    return n.kind == rv_editor_tile_kind::split && rv_editor_strip_leaf(ws, n.split.first) &&
        rv_editor_strip_leaf(ws, n.split.second);
}

// `node`'s need along `axis`: a strip leaf's own minimum, or a strip row's, which
// is its children's minimums summed on the row's own axis and the largest of them
// on the other.
int32_t rv_editor_strip_like_min_along(const rv_editor_workspace &ws, uint32_t node, rv_editor_axis axis)
{
    if (ws.layout.nodes[node].kind != rv_editor_tile_kind::split) {
        return rv_editor_strip_min_along(ws, node, axis);
    }
    const rv_editor_tile_split &split = ws.layout.nodes[node].split;
    const int32_t first = rv_editor_strip_min_along(ws, split.first, axis);
    const int32_t second = rv_editor_strip_min_along(ws, split.second, axis);
    return split.axis == axis ? first + second : std::max(first, second);
}

// Sets every split with a strip child (a strip leaf, or a strip row) to that
// strip's exact minimum, the rest going to its sibling: no empty band under a
// strip's rows. Skipped once the tree no longer fits its minimums
// (rv_editor_tile_place_node then shrinks both children in proportion instead of
// honoring any ratio). `all` is false in a layout the user has touched: only the
// transport strips are fitted there, and by height only: as their height follows their width, while the width is the user's.
void rv_editor_shell_fit_strips(rv_editor_workspace &ws, bool all)
{
    for (uint32_t i = 0; i < ws.layout.nodes.size(); ++i) {
        const rv_editor_tile_node &n = ws.layout.nodes[i];
        if (n.kind != rv_editor_tile_kind::split || i >= ws.rects.size()) {
            continue;
        }
        const bool first_is_strip = rv_editor_strip_leaf(ws, n.split.first) || rv_editor_strip_row(ws, n.split.first);
        const bool second_is_strip = rv_editor_strip_leaf(ws, n.split.second) || rv_editor_strip_row(ws, n.split.second);
        if (first_is_strip == second_is_strip) {
            continue; // neither, or both: nothing to fit against
        }
        const rv_editor_axis axis = n.split.axis;
        if (!all && axis != rv_editor_axis::y) {
            continue; // a strip's width beside a pane is the user's
        }
        const rv_editor_rect &a = ws.rects[n.split.first];
        const rv_editor_rect &b = ws.rects[n.split.second];
        const int32_t total = (axis == rv_editor_axis::x ? a.w : a.h) + (axis == rv_editor_axis::x ? b.w : b.h);
        if (total <= 0) {
            continue;
        }
        const uint32_t strip = first_is_strip ? n.split.first : n.split.second;
        if (!all && !rv_editor_transport_leaf(ws, strip)) {
            continue;
        }
        const int32_t need = rv_editor_strip_like_min_along(ws, strip, axis);
        const float ratio = first_is_strip ? static_cast<float>(need) / static_cast<float>(total) :
                                             1.0f - static_cast<float>(need) / static_cast<float>(total);
        (void)rv_editor_tile_set_ratio(ws.layout, i, ratio);
    }
}

// The Game's picture height the fit aims at, from the work area's height `h` (the window less the menu and status bars,
// 48 px at scale 1): linear through the approved sizes, 300 px at 720 (work area 672) and 480 at 1080 (1032); in the
// Scene, whose column is narrower, 240 and 360. The tiles' minimums can only lower it, so it never depends on history.
int32_t rv_editor_game_target_h(rv_editor_layout_preset preset, int32_t h)
{
    return preset == rv_editor_layout_preset::scene ? h / 3 + 16 : h / 2 - 36;
}

// An untouched tree whose first column is the Files tile (Code): Files is 176 px at 1280 and 240 at 1920, a tenth of the
// window's width plus 48, so the tile is the same in a window of either size, whatever size it was drawn in before.
void rv_editor_shell_fit_files(rv_editor_workspace &ws)
{
    const rv_editor_tile_node &root = ws.layout.nodes[ws.layout.root];
    if (root.kind != rv_editor_tile_kind::split || root.split.axis != rv_editor_axis::x) {
        return;
    }
    const rv_editor_tile_node &first = ws.layout.nodes[root.split.first];
    if (first.kind != rv_editor_tile_kind::leaf || first.leaf.tabs.size() != 1 ||
        ws.panes.panes[first.leaf.tabs[0]].kind != rv_editor_pane_kind::files) {
        return;
    }
    const int32_t room = ws.rects[ws.layout.root].w - ws.tile_metrics.bar;
    if (room > 0) {
        (void)rv_editor_tile_set_ratio(ws.layout,
            ws.layout.root,
            static_cast<float>(ws.rects[ws.layout.root].w / 10 + 48) / static_cast<float>(room));
    }
}

} // namespace

// Every preset has a Game, so every one fits its tiles to the picture.
bool rv_editor_preset_fits_game(rv_editor_layout_preset)
{
    return true;
}

void rv_editor_shell_switch(rv_editor_shell &shell, rv_editor_layout_preset to)
{
    if (to == shell.active) {
        return;
    }
    shell.trees[rv_editor_workspace_slot(shell.active)] = std::move(shell.ws.layout);
    shell.ws.layout = std::move(shell.trees[rv_editor_workspace_slot(to)]);
    shell.active = to;
    shell.ws.focused_leaf = rv_editor_tile_none;
    // The Game's area and the rectangles are the other tree's until the next draw.
    shell.ui.game_area = { 0, 0 };
    shell.ws.rects.clear();
}

void rv_editor_shell_reset_layout(rv_editor_shell &shell, rv_editor_layout_preset preset)
{
    shell.ws.layout = rv_editor_workspace_start(shell.ws.panes, preset);
    shell.ws.focused_leaf = rv_editor_tile_none;
    shell.game_fit[rv_editor_workspace_slot(preset)] = rv_editor_preset_fits_game(preset);
    shell.layout_untouched[rv_editor_workspace_slot(preset)] = true;
    shell.ui.game_area = { 0, 0 };
}

// Assets asked for a layout (double click): switched before this frame's open requests are taken.
void rv_editor_shell_take_layout_request(rv_editor_shell &shell)
{
    if (shell.app.layout_request) {
        rv_editor_shell_switch(shell, *shell.app.layout_request);
        shell.app.layout_request.reset();
    }
}

void rv_editor_shell_frame_start(rv_editor_shell &shell)
{
    rv_editor_shell_take_layout_request(shell);
    // Untouched (reset/fresh preset, no splitter dragged since) and rects from
    // the last draw of this very tree. Independent of game_fit: a Game the user
    // stopped fitting leaves the strips fitting.
    const size_t slot = rv_editor_workspace_slot(shell.active);
    if (shell.ws.rects.size() == shell.ws.layout.nodes.size()) {
        rv_editor_shell_fit_strips(shell.ws, shell.layout_untouched[slot] && !shell.ws.dragged);
        if (shell.layout_untouched[slot] && !shell.ws.dragged) {
            rv_editor_shell_fit_files(shell.ws);
        }
    }
    rv_editor_shell_fit_game(shell);
}

void rv_editor_shell_fit_game(rv_editor_shell &shell)
{
    const size_t slot = rv_editor_workspace_slot(shell.active);
    // A splitter the user dragged is the user's: the tree keeps it from then on.
    if (shell.ws.dragged) {
        shell.ws.dragged = false;
        shell.game_fit[slot] = false;
        shell.layout_untouched[slot] = false;
    }
    rv_editor_workspace &ws = shell.ws;
    const rv_editor_size area = shell.ui.game_area;
    // Nothing drawn yet (the start screen, no project, a tree just shown): wait.
    if (!shell.game_fit[slot] || area.w <= 0 || area.h <= 0 || ws.layout.maximized_leaf != rv_editor_tile_none ||
        ws.rects.size() != ws.layout.nodes.size()) {
        return;
    }
    for (size_t i = 0; i < ws.panes.panes.size(); ++i) {
        if (ws.panes.panes[i].kind == rv_editor_pane_kind::game) {
            rv_editor_fit_game_tree(ws,
                static_cast<rv_editor_pane_id>(i),
                area,
                shell.app.project.screen_w,
                shell.app.project.screen_h,
                rv_editor_game_target_h(shell.active, ws.rects[ws.layout.root].h),
                [&ws](uint32_t n) {
                    return rv_editor_strip_leaf(ws, n) || rv_editor_strip_row(ws, n);
                });
            return;
        }
    }
}

// Legacy view-file keys from an earlier editor: "test" for Debug, "release" for Burn.
constexpr const char *legacy_debug_key = "test";
constexpr const char *legacy_burn_key = "release";

void rv_editor_shell_load_layouts(rv_editor_shell &shell, const std::filesystem::path &path, const std::string &active)
{
    shell.ws.panes = {};
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        rv_editor_layout &tree = shell.trees[rv_editor_workspace_slot(preset)];
        rv_editor_pane_registry own;
        rv_editor_layout layout;
        std::filesystem::path file = path.empty() ? path : rv_editor_workspace_file(path, preset);
        std::error_code ec;
        if (!file.empty() && !std::filesystem::exists(file, ec) && !rv_editor_workspace_old_file(path, preset).empty()) {
            file = rv_editor_workspace_old_file(path, preset);
        }
        std::string error;
        if (!file.empty() && rv_editor_layout_load(file, own, layout, error) == RV_OK) {
            rv_editor_pane_adopt(shell.ws.panes, own, layout);
            tree = std::move(layout);
            continue;
        }
        if (!file.empty() && std::filesystem::exists(file, ec)) {
            std::fprintf(stderr,
                "3dmppc-editor: layout not read: %s; starting from %s\n",
                error.c_str(),
                rv_editor_layout_preset_name(preset));
        }
        tree = rv_editor_workspace_start(shell.ws.panes, preset);
        shell.game_fit[rv_editor_workspace_slot(preset)] = rv_editor_preset_fits_game(preset);
        shell.layout_untouched[rv_editor_workspace_slot(preset)] = true;
    }
    std::string key;
    if (active == legacy_debug_key) {
        key = rv_editor_workspace_key_of(rv_editor_layout_preset::debug);
    } else if (active == legacy_burn_key) {
        key = rv_editor_workspace_key_of(rv_editor_layout_preset::burn);
    } else {
        key = active;
    }
    shell.active = rv_editor_layout_preset::code;
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        if (key == rv_editor_workspace_key_of(preset)) {
            shell.active = preset;
        }
    }
    shell.ws.layout = std::move(shell.trees[rv_editor_workspace_slot(shell.active)]);
    shell.ws.focused_leaf = rv_editor_tile_none;
}

int rv_editor_shell_save_layouts(const rv_editor_shell &shell, const std::filesystem::path &path, std::string &error)
{
    int result = RV_OK;
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        const rv_editor_layout &tree = preset == shell.active ? shell.ws.layout : shell.trees[rv_editor_workspace_slot(preset)];
        std::string why;
        const int err = rv_editor_layout_save(rv_editor_workspace_file(path, preset), shell.ws.panes, tree, why);
        if (err != RV_OK && result == RV_OK) {
            error = why;
            result = err;
        }
    }
    return result;
}

const char *rv_editor_shell_workspace_key(const rv_editor_shell &shell)
{
    return rv_editor_workspace_key_of(shell.active);
}

bool rv_editor_shell_pane_kept(const rv_editor_shell &shell, rv_editor_pane_id pane)
{
    if (rv_editor_tile_find(shell.ws.layout, pane) != rv_editor_tile_none) {
        return true;
    }
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        if (preset != shell.active &&
            rv_editor_tile_find(shell.trees[rv_editor_workspace_slot(preset)], pane) != rv_editor_tile_none) {
            return true;
        }
    }
    return false;
}

} // namespace rv_editor
