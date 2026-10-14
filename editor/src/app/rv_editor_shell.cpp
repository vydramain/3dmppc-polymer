// The window's models loop and pane dispatch.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <system_error>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "panes/rv_editor_panes.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// A new tile splits its neighbour in half.
constexpr float rv_editor_even_split = 0.5f;

// The leaf a new pane goes to: the focused one, else the maximized one, else the
// first leaf of the tree, so a menu command works with no tile focused.
uint32_t rv_editor_target_leaf(const rv_editor_workspace &ws)
{
    const auto is_leaf = [&ws](uint32_t n) {
        return n < ws.layout.nodes.size() && ws.layout.nodes[n].kind == rv_editor_tile_kind::leaf;
    };
    if (is_leaf(ws.focused_leaf)) {
        return ws.focused_leaf;
    }
    if (is_leaf(ws.layout.maximized_leaf)) {
        return ws.layout.maximized_leaf;
    }
    uint32_t n = ws.layout.root;
    while (n < ws.layout.nodes.size() && ws.layout.nodes[n].kind == rv_editor_tile_kind::split) {
        n = ws.layout.nodes[n].split.first;
    }
    return n;
}

// The biggest leaf at the last draw, else the target leaf: where a page's tab has room.
uint32_t rv_editor_roomy_leaf(const rv_editor_workspace &ws)
{
    uint32_t best = rv_editor_tile_none;
    int64_t area = -1;
    for (uint32_t n = 0; n < ws.layout.nodes.size() && n < ws.rects.size(); ++n) {
        const int64_t a = static_cast<int64_t>(ws.rects[n].w) * ws.rects[n].h;
        if (ws.layout.nodes[n].kind == rv_editor_tile_kind::leaf && a > area) {
            best = n;
            area = a;
        }
    }
    return best != rv_editor_tile_none ? best : rv_editor_target_leaf(ws);
}

// Brings a pane of `kind` to the front where the tree already shows one, or adds
// one as a tab of the target leaf; a `roomy` one goes to the biggest leaf instead.
rv_editor_pane_id rv_editor_shell_show(rv_editor_workspace &ws, rv_editor_pane_kind kind, bool roomy = false)
{
    for (const rv_editor_tile_node &node : ws.layout.nodes) {
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id pane : node.leaf.tabs) {
            if (ws.panes.panes[pane].kind == kind) {
                (void)rv_editor_tile_activate(ws.layout, pane);
                return pane;
            }
        }
    }
    const rv_editor_pane_id pane = rv_editor_pane_add(ws.panes, kind);
    rv_editor_tile_insert(ws.layout,
        roomy ? rv_editor_roomy_leaf(ws) : rv_editor_target_leaf(ws),
        pane,
        rv_editor_tile_dock::tab);
    (void)rv_editor_tile_activate(ws.layout, pane);
    return pane;
}

constexpr std::optional<rv_editor_pane_kind> rv_editor_page_pane[] = { std::nullopt,
    std::nullopt,
    rv_editor_pane_kind::open_project,
    rv_editor_pane_kind::settings,
    rv_editor_pane_kind::help,
    rv_editor_pane_kind::manual };
static_assert(std::size(rv_editor_page_pane) == static_cast<size_t>(rv_editor_start_page::manual) + 1);

} // namespace
void rv_editor_shell_page(rv_editor_shell &shell, rv_editor_start_page page)
{
    if (!shell.app.project.open) {
        shell.start_page = page;
        return;
    }
    if (const auto kind = rv_editor_page_pane[static_cast<size_t>(page)]) {
        rv_editor_shell_show(shell.ws, *kind, true);
    }
}

void rv_editor_shell_open_project(rv_editor_shell &shell)
{
    rv_editor_shell_page(shell, rv_editor_start_page::open_project);
}

void rv_editor_shell_show_pane(rv_editor_shell &shell, rv_editor_pane_kind kind, bool roomy)
{
    rv_editor_shell_show(shell.ws, kind, roomy);
}

void rv_editor_shell_focus_terminal(rv_editor_shell &shell)
{
    const rv_editor_pane_id pane = rv_editor_shell_show(shell.ws, rv_editor_pane_kind::terminal);
    const uint32_t leaf = rv_editor_tile_find(shell.ws.layout, pane);
    if (leaf != rv_editor_tile_none) {
        shell.ws.focused_leaf = leaf;
    }
    shell.app.terminals[pane].focus_request = true;
}

void rv_editor_shell_focus_next(rv_editor_shell &shell, bool back)
{
    rv_editor_workspace &ws = shell.ws;
    // Depth first, first child first: left before right, top before bottom.
    std::vector<uint32_t> leaves;
    std::vector<uint32_t> stack{ ws.layout.root };
    while (!stack.empty()) {
        const uint32_t n = stack.back();
        stack.pop_back();
        if (n >= ws.layout.nodes.size()) {
            continue;
        }
        const rv_editor_tile_node &node = ws.layout.nodes[n];
        if (node.kind == rv_editor_tile_kind::leaf) {
            leaves.push_back(n);
        } else if (node.kind == rv_editor_tile_kind::split) {
            stack.push_back(node.split.second);
            stack.push_back(node.split.first);
        }
    }
    // A maximized tile is the only one on screen.
    if (ws.layout.maximized_leaf != rv_editor_tile_none) {
        leaves = { ws.layout.maximized_leaf };
    }
    if (leaves.empty()) {
        return;
    }
    const auto at = std::find(leaves.begin(), leaves.end(), ws.focused_leaf);
    size_t i = 0;
    if (at != leaves.end()) {
        const size_t here = static_cast<size_t>(at - leaves.begin());
        i = back ? (here + leaves.size() - 1) % leaves.size() : (here + 1) % leaves.size();
    }
    ws.focused_leaf = leaves[i];
    ws.focus_request = leaves[i];
}

void rv_editor_shell_new_tile(rv_editor_shell &shell, rv_editor_pane_kind kind)
{
    rv_editor_workspace &ws = shell.ws;
    const rv_editor_pane_id pane = rv_editor_pane_add(ws.panes, kind);
    const uint32_t leaf = rv_editor_tile_insert(ws.layout, rv_editor_target_leaf(ws), pane, rv_editor_tile_dock::right);
    if (leaf != rv_editor_tile_none) {
        (void)rv_editor_tile_set_ratio(ws.layout, ws.layout.nodes[leaf].parent, rv_editor_even_split);
        ws.focused_leaf = leaf;
    }
}

void rv_editor_shell_game_input(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    // A new console starts with every key up: a capture made for the last console does not carry over to the next one.
    const bool new_session = app.session.number() != shell.game_session;
    shell.game_session = app.session.number();
    // A Game tile closed or behind another tab, a window without the keyboard or
    // no console: the capture ends here, whatever the pane saw.
    const bool capture_lost = new_session || !shell.ui.game_drawn || !shell.window_focused || !app.session.live();
    if (capture_lost) {
        shell.ui.game_captured = false;
    }
    // The shell owns the Game's frame state and resets it itself: a pane only reports the draw.
    shell.ui.game_drawn = false;
    uint64_t buttons = 0;
    if (shell.ui.game_captured) {
        // ImGui's keyboard navigation stays off the next frame, so arrows stay the game's.
        shell.ui.text_focus = true;
        if (app.session.state() == rv_editor_run_state::running) {
            buttons = rv_editor_game_keys();
        }
    }
    // Only a change is sent; a refused one is tried again next frame, its warning once per run of refusals.
    if (!app.session.pad_pending(buttons)) {
        shell.pad_refusal_reported = false;
        return;
    }
    std::string error;
    const int pad_result = app.session.pad(buttons, app.log, error);
    const bool refused = pad_result != RV_OK;
    if (refused && !shell.pad_refusal_reported) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "cannot send the pad state: " + error);
    }
    shell.pad_refusal_reported = refused;
}

int rv_editor_shell_pace_apply(rv_editor_shell &shell)
{
    const bool set = SDL_SetRenderVSync(shell.renderer, shell.pace.vsync ? 1 : 0);
    const std::string set_error = set ? std::string() : SDL_GetError();
    // The window's own answer is what counts: vsync is active only when SDL reports it on.
    int on = 0;
    const bool read = SDL_GetRenderVSync(shell.renderer, &on);
    shell.vsync_active = read && on != 0;
    if (!read) {
        shell.app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "cannot read vsync: " + std::string(SDL_GetError()));
    }
    if (!set) {
        const std::string limit = std::to_string(shell.pace.fps_limit);
        const std::string text = shell.pace.vsync ?
            "vsync did not turn on (" + set_error + "); frames are limited to " + limit + " per second" :
            "vsync did not turn off: " + set_error;
        shell.app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, text);
    }
    return set && read ? RV_OK : RV_ERR_IO;
}

} // namespace rv_editor
