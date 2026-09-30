// The layouts Code, Scene, Debug and Burn: choosing one, resetting it, their files.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <string>
#include <system_error>

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
        case rv_editor_layout_preset::scene: return "scene";
        case rv_editor_layout_preset::debug: return "debug";
        case rv_editor_layout_preset::burn: return "burn";
        default: return "code";
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
        case rv_editor_layout_preset::code: return path;
        case rv_editor_layout_preset::debug: return path.parent_path() / (path.filename().string() + "-test");
        case rv_editor_layout_preset::burn: return path.parent_path() / (path.filename().string() + "-release");
        default: return {};
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

// Debug keeps its Runtime Log's height: fitting the tiles to the Game there would
// give the log's rows to the picture, which Fit draws whole in any tile anyway.
bool rv_editor_preset_fits_game(rv_editor_layout_preset preset)
{
    return preset != rv_editor_layout_preset::debug;
}

// Walks from `node` up to the first ancestor split on `axis` whose sibling is not a
// controls strip, and grows `node`'s share there by `delta` (>= 0). When
// `guard_bottom_sibling` holds, an ancestor whose sibling is the bottom (second)
// child is skipped instead: height never comes from a bottom row. Placement
// clamps the resulting ratio to every pane's minimum on its own, so a grow that
// would push a neighbor below it simply lands at that minimum, not past it.
// False, changing nothing, when no eligible ancestor exists.
bool rv_editor_shell_grow_game(rv_editor_workspace &ws, uint32_t node, rv_editor_axis axis, int32_t delta,
    bool guard_bottom_sibling)
{
    for (uint32_t parent = ws.layout.nodes[node].parent; parent != rv_editor_tile_none;
         node = parent, parent = ws.layout.nodes[parent].parent) {
        const rv_editor_tile_split &split = ws.layout.nodes[parent].split;
        const uint32_t sibling = node == split.first ? split.second : split.first;
        if (ws.layout.nodes[parent].kind != rv_editor_tile_kind::split || split.axis != axis ||
            rv_editor_strip_leaf(ws, sibling) || (guard_bottom_sibling && sibling == split.second)) {
            continue;
        }
        const rv_editor_rect &a = ws.rects[split.first];
        const rv_editor_rect &b = ws.rects[split.second];
        const int32_t first = axis == rv_editor_axis::x ? a.w : a.h;
        const int32_t total = first + (axis == rv_editor_axis::x ? b.w : b.h);
        if (total <= 0) {
            continue;
        }
        const int32_t want = first + (node == split.first ? delta : -delta);
        rv_editor_tile_set_ratio(ws.layout, parent, static_cast<float>(want) / static_cast<float>(total));
        return true;
    }
    return false;
}

// `node`'s size along `axis`, the largest of its tabs' minimums (ws.minimums,
// filled from shell.strips): what a strip leaf needs and nothing more.
int32_t rv_editor_strip_min_along(const rv_editor_workspace &ws, uint32_t node, rv_editor_axis axis)
{
    rv_editor_size need{ 0, 0 };
    for (const rv_editor_pane_id pane : ws.layout.nodes[node].leaf.tabs) {
        const auto it = ws.minimums.find(pane);
        if (it != ws.minimums.end()) {
            need.w = std::max(need.w, it->second.w);
            need.h = std::max(need.h, it->second.h);
        }
    }
    return axis == rv_editor_axis::x ? need.w : need.h;
}

// A split holding only strip leaves side by side, such as Runtime Controls beside
// the Session Toolchest: not a strip itself, but it needs no more than the
// tallest (or widest) of the strips it holds.
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
// honoring any ratio).
void rv_editor_shell_fit_strips(rv_editor_workspace &ws)
{
    for (uint32_t i = 0; i < ws.layout.nodes.size(); ++i) {
        const rv_editor_tile_node &n = ws.layout.nodes[i];
        if (n.kind != rv_editor_tile_kind::split || i >= ws.rects.size()) {
            continue;
        }
        const bool first_is_strip = rv_editor_strip_leaf(ws, n.split.first) || rv_editor_strip_row(ws, n.split.first);
        const bool second_is_strip =
            rv_editor_strip_leaf(ws, n.split.second) || rv_editor_strip_row(ws, n.split.second);
        if (first_is_strip == second_is_strip) {
            continue; // neither, or both: nothing to fit against
        }
        const rv_editor_axis axis = n.split.axis;
        const rv_editor_rect &a = ws.rects[n.split.first];
        const rv_editor_rect &b = ws.rects[n.split.second];
        const int32_t total = (axis == rv_editor_axis::x ? a.w : a.h) + (axis == rv_editor_axis::x ? b.w : b.h);
        if (total <= 0) {
            continue;
        }
        const uint32_t strip = first_is_strip ? n.split.first : n.split.second;
        const int32_t need = rv_editor_strip_like_min_along(ws, strip, axis);
        const float ratio = first_is_strip ? static_cast<float>(need) / static_cast<float>(total)
                                            : 1.0f - static_cast<float>(need) / static_cast<float>(total);
        rv_editor_tile_set_ratio(ws.layout, i, ratio);
    }
}

} // namespace

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
    shell.app.game_area = { 0, 0 };
    shell.ws.rects.clear();
    shell.game_fit_tries = 8;
}

void rv_editor_shell_reset_layout(rv_editor_shell &shell, rv_editor_layout_preset preset)
{
    shell.ws.layout = rv_editor_workspace_start(shell.ws.panes, preset);
    shell.ws.focused_leaf = rv_editor_tile_none;
    shell.game_fit[rv_editor_workspace_slot(preset)] = rv_editor_preset_fits_game(preset);
    shell.layout_untouched[rv_editor_workspace_slot(preset)] = true;
    shell.game_fit_tries = 8;
    shell.app.game_area = { 0, 0 };
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
    // the last draw of this very tree. Independent of game_fit: Debug does not
    // fit the Game but its strips still fit.
    const size_t slot = rv_editor_workspace_slot(shell.active);
    if (shell.layout_untouched[slot] && !shell.ws.dragged && shell.ws.rects.size() == shell.ws.layout.nodes.size()) {
        rv_editor_shell_fit_strips(shell.ws);
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
    const rv_editor_layout &layout = shell.ws.layout;
    const rv_editor_size area = shell.app.game_area;
    // Nothing drawn yet (the start screen, no project, a tree just shown): wait.
    if (!shell.game_fit[slot] || area.w <= 0 || area.h <= 0 || layout.maximized_leaf != rv_editor_tile_none ||
        shell.ws.rects.size() != layout.nodes.size()) {
        return;
    }
    uint32_t node = rv_editor_tile_none;
    for (size_t i = 0; i < shell.ws.panes.panes.size() && node == rv_editor_tile_none; ++i) {
        if (shell.ws.panes.panes[i].kind == rv_editor_pane_kind::game) {
            node = rv_editor_tile_find(layout, static_cast<rv_editor_pane_id>(i));
        }
    }
    if (node == rv_editor_tile_none) {
        return;
    }
    // Whole steps of the screen's proportion, 4 x 3 for 320 x 240: a picture area of
    // whole steps takes the frame at Fit with not one pixel over.
    const int64_t sw = std::max<int64_t>(1, shell.app.project.screen_w);
    const int64_t sh = std::max<int64_t>(1, shell.app.project.screen_h);
    const int64_t step_w = sw / std::gcd(sw, sh);
    const int64_t step_h = sh / std::gcd(sw, sh);
    // The smallest step count at or beyond both current dimensions: reaching it
    // only grows the Game tile, on either axis, and never shrinks it.
    const int64_t steps = std::max((area.w + step_w - 1) / step_w, (area.h + step_h - 1) / step_h);
    const int32_t delta_w = static_cast<int32_t>(steps * step_w - area.w);
    const int32_t delta_h = static_cast<int32_t>(steps * step_h - area.h);
    if (delta_w == 0 && delta_h == 0) {
        shell.game_fit_tries = 8;
        return;
    }
    // Eight tries to settle, then the tree is left alone: minimums may allow no exact fit.
    if (shell.game_fit_tries == 0) {
        shell.game_fit[slot] = false;
        return;
    }
    --shell.game_fit_tries;
    // Width first; a try that changed nothing last time is stuck on a minimum,
    // so height gets the turn instead. Growing height never takes from a bottom
    // row (rv_editor_shell_grow_game skips a split whose sibling is one).
    const bool held = area.w == shell.game_fit_last.w && area.h == shell.game_fit_last.h;
    shell.game_fit_last = area;
    if (delta_w > 0 && !held && rv_editor_shell_grow_game(shell.ws, node, rv_editor_axis::x, delta_w, false)) {
        return;
    }
    if (delta_h > 0) {
        rv_editor_shell_grow_game(shell.ws, node, rv_editor_axis::y, delta_h, true);
    }
}

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
        if (!file.empty() && rv_editor_layout_load(file, own, layout)) {
            rv_editor_pane_adopt(shell.ws.panes, own, layout);
            tree = std::move(layout);
            continue;
        }
        if (!file.empty() && std::filesystem::exists(file, ec)) {
            std::fprintf(stderr, "3dmppc-editor: %s is not a layout this editor reads; starting from %s\n",
                file.c_str(), rv_editor_layout_preset_name(preset));
        }
        tree = rv_editor_workspace_start(shell.ws.panes, preset);
        shell.game_fit[rv_editor_workspace_slot(preset)] = rv_editor_preset_fits_game(preset);
        shell.layout_untouched[rv_editor_workspace_slot(preset)] = true;
    }
    // "test" and "release" are what an earlier editor wrote for Debug and Burn.
    const std::string key = active == "test" ? "debug" : active == "release" ? "burn" : active;
    shell.active = rv_editor_layout_preset::code;
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        if (key == rv_editor_workspace_key_of(preset)) {
            shell.active = preset;
        }
    }
    shell.ws.layout = std::move(shell.trees[rv_editor_workspace_slot(shell.active)]);
    shell.ws.focused_leaf = rv_editor_tile_none;
}

bool rv_editor_shell_save_layouts(const rv_editor_shell &shell, const std::filesystem::path &path, std::string &error)
{
    bool ok = true;
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        const rv_editor_layout &tree =
            preset == shell.active ? shell.ws.layout : shell.trees[rv_editor_workspace_slot(preset)];
        std::string why;
        if (!rv_editor_layout_save(rv_editor_workspace_file(path, preset), shell.ws.panes, tree, why) && ok) {
            error = why;
            ok = false;
        }
    }
    return ok;
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
