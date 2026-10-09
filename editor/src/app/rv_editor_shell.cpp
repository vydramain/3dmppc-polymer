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

// Code panes the tree shows now.
std::vector<rv_editor_pane_id> rv_editor_code_panes(const rv_editor_workspace &ws)
{
    std::vector<rv_editor_pane_id> out;
    for (const rv_editor_tile_node &node : ws.layout.nodes) {
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id pane : node.leaf.tabs) {
            if (ws.panes.panes[pane].kind == rv_editor_pane_kind::code) {
                out.push_back(pane);
            }
        }
    }
    return out;
}

// Where a file the user opened goes: the focused tile's code pane, the code
// pane used last, any code pane, or a new one in the focused tile.
rv_editor_pane_id rv_editor_code_target(rv_editor_workspace &ws, rv_editor_pane_id last)
{
    if (ws.focused_leaf < ws.layout.nodes.size() && ws.layout.nodes[ws.focused_leaf].kind == rv_editor_tile_kind::leaf) {
        const rv_editor_tile_leaf &leaf = ws.layout.nodes[ws.focused_leaf].leaf;
        if (!leaf.tabs.empty() && ws.panes.panes[leaf.tabs[leaf.active]].kind == rv_editor_pane_kind::code) {
            return leaf.tabs[leaf.active];
        }
    }
    const std::vector<rv_editor_pane_id> code = rv_editor_code_panes(ws);
    if (std::find(code.begin(), code.end(), last) != code.end()) {
        (void)rv_editor_tile_activate(ws.layout, last);
        return last;
    }
    if (!code.empty()) {
        (void)rv_editor_tile_activate(ws.layout, code.front());
        return code.front();
    }
    const bool focused =
        ws.focused_leaf < ws.layout.nodes.size() && ws.layout.nodes[ws.focused_leaf].kind == rv_editor_tile_kind::leaf;
    const rv_editor_pane_id pane = rv_editor_pane_add(ws.panes, rv_editor_pane_kind::code);
    rv_editor_tile_insert(ws.layout, focused ? ws.focused_leaf : ws.layout.root, pane, rv_editor_tile_dock::tab);
    (void)rv_editor_tile_activate(ws.layout, pane);
    return pane;
}

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

// A closed tile's shell is hung up here and ended by the frame steps; nothing waits.
void rv_editor_shell_stop_terminal(rv_editor_app &app, rv_editor_terminal_view &view)
{
    if (view.term == nullptr) {
        return;
    }
    std::unique_ptr<rv_editor_pty> pty = view.term->release_pty();
    std::string error;
    if (pty->begin_stop(error) != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "terminal: " + error);
    }
    app.stopping_ptys.push_back(std::move(pty));
}

// The action an unsaved ask stands for, and the question it opens on the shell.
struct rv_editor_ask_question {
    rv_editor_unsaved_ask ask;
    rv_editor_shell::rv_editor_leave leave;
    const char *action; // named in the log when another question refuses it
};
constexpr rv_editor_ask_question rv_editor_ask_questions[] = {
    { rv_editor_unsaved_ask::build, rv_editor_shell::rv_editor_leave::build, "build" },
    { rv_editor_unsaved_ask::run, rv_editor_shell::rv_editor_leave::run, "run" },
    { rv_editor_unsaved_ask::build_restart, rv_editor_shell::rv_editor_leave::build_restart, "build and restart" },
};

// RV_OK: the question is open. RV_ERR_BUSY: another question is open, so the action
// does not start and the log says so. The shell asks one thing at a time.
int rv_editor_shell_open_question(rv_editor_shell &shell, rv_editor_unsaved_ask ask)
{
    const auto found = std::find_if(std::begin(rv_editor_ask_questions),
        std::end(rv_editor_ask_questions),
        [ask](const rv_editor_ask_question &question) {
            return question.ask == ask;
        });
    if (found == std::end(rv_editor_ask_questions)) {
        shell.app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "unsaved-files question: unknown action " + std::to_string(static_cast<int>(ask)));
        return RV_ERR_INVAL;
    }
    if (shell.leaving != rv_editor_shell::rv_editor_leave::none) {
        shell.app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            std::string(found->action) + " not started: another question is open");
        return RV_ERR_BUSY;
    }
    shell.leaving = found->leave;
    shell.app.unsaved_open = ask;
    return RV_OK;
}
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

void rv_editor_shell_update(rv_editor_shell &shell)
{
    rv_editor_shell_frame_start(shell);
    rv_editor_app_update(shell.app);
    // Build, Run or Build and Restart met unsaved files: the question opens, or the refusal (or an unknown action) is logged.
    if (shell.app.unsaved_ask != rv_editor_unsaved_ask::none) {
        (void)rv_editor_shell_open_question(shell, shell.app.unsaved_ask);
    }
    shell.app.unsaved_ask = rv_editor_unsaved_ask::none;
    rv_editor_shell_after_save(shell);
    rv_editor_shell_review_tab(shell);
    rv_editor_shell_settings_apply(shell);

    rv_editor_app &requests = shell.app;
    requests.preset = shell.active;
    requests.release_view = shell.active == rv_editor_layout_preset::burn;
    if (requests.show_request != rv_editor_pane_kind::empty) {
        // Run Configuration's list and form need room: the biggest tile takes it.
        rv_editor_shell_show(shell.ws, requests.show_request, requests.show_request == rv_editor_pane_kind::run_config);
        requests.show_request = rv_editor_pane_kind::empty;
    }
    if (requests.new_file_request) {
        requests.new_file_request = false;
        rv_editor_shell_new_file(shell);
    }
    if (shell.app.open_folder_request) {
        shell.app.open_folder_request = false;
        rv_editor_shell_open_project(shell);
    }

    // A code pane that left every workspace's tree, whatever took it (close,
    // another kind, a layout from the menu), gives its nvim window back; one in a
    // workspace not shown keeps it.
    for (const uint32_t pane : shell.app.nvim.panes()) {
        if (shell.ws.panes.panes[pane].kind != rv_editor_pane_kind::code || !rv_editor_shell_pane_kept(shell, pane)) {
            shell.app.nvim.release(pane);
            shell.app.code_tabs.erase(pane);
        }
    }

    // The code tile in front of the focused tile is the one used last.
    if (shell.ws.focused_leaf < shell.ws.layout.nodes.size() &&
        shell.ws.layout.nodes[shell.ws.focused_leaf].kind == rv_editor_tile_kind::leaf) {
        const rv_editor_tile_leaf &leaf = shell.ws.layout.nodes[shell.ws.focused_leaf].leaf;
        if (!leaf.tabs.empty() && shell.ws.panes.panes[leaf.tabs[leaf.active]].kind == rv_editor_pane_kind::code) {
            shell.last_code = leaf.tabs[leaf.active];
        }
    }
    // Until one is used, the first code tile is the one Save, Undo and Redo act on.
    if (shell.last_code == rv_editor_tile_none || !rv_editor_shell_pane_kept(shell, shell.last_code)) {
        const std::vector<rv_editor_pane_id> code = rv_editor_code_panes(shell.ws);
        shell.last_code = code.empty() ? rv_editor_tile_none : code.front();
    }
    // last_code's file, for Reload; empty unless the tile already has a window.
    shell.app.code_file.clear();
    if (const auto held = shell.app.nvim.panes(); std::find(held.begin(), held.end(), shell.last_code) != held.end()) {
        const rv_editor_nvim_buffer *buf = shell.app.nvim.buffer_in(shell.app.nvim.window_for(shell.last_code));
        if (buf != nullptr && !buf->name.empty()) {
            shell.app.code_file = buf->name;
        }
    }

    // A Terminal tile that left every workspace's tree, or became another kind,
    // ends its shell.
    for (auto it = shell.app.terminals.begin(); it != shell.app.terminals.end();) {
        const rv_editor_pane_id pane = it->first;
        if (pane < shell.ws.panes.panes.size() && shell.ws.panes.panes[pane].kind == rv_editor_pane_kind::terminal &&
            rv_editor_shell_pane_kept(shell, pane)) {
            ++it;
            continue;
        }
        rv_editor_shell_stop_terminal(shell.app, it->second);
        it = shell.app.terminals.erase(it);
    }

    // Files the user opened go to a code tile once its window exists. A binary
    // one opens as text only on the user's word: nvim would show it as noise.
    rv_editor_app &app = shell.app;
    std::erase_if(app.open_requests, [&app](const rv_editor_open_request &request) {
        uintmax_t size = 0;
        if (app.open_as_text.erase(request.path) != 0 || !rv_editor_file_binary(request.path, size)) {
            return false;
        }
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            rv_editor_shell_buffer_label(app, request.path.string()) + " is binary (" + std::to_string(size) +
                " bytes), not opened as text: Files > Open as Text shows it anyway");
        return true;
    });
    if (!app.open_requests.empty() && !app.nvim.problem.empty()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "cannot open " + app.open_requests.front().path.string() + ": " + app.nvim.problem);
        app.open_requests.clear();
    }
    if (!app.open_requests.empty()) {
        const rv_editor_pane_id pane = rv_editor_code_target(shell.ws, shell.last_code);
        const uint32_t leaf = rv_editor_tile_find(shell.ws.layout, pane);
        // A maximized tile that hides the code tile steps back, or its window never draws.
        if (shell.ws.layout.maximized_leaf != rv_editor_tile_none && shell.ws.layout.maximized_leaf != leaf) {
            shell.ws.layout.maximized_leaf = rv_editor_tile_none;
        }
        const int64_t win = app.nvim.window_for(pane);
        if (win != 0) {
            for (const auto &request : app.open_requests) {
                app.nvim.open(win, request.path, request.line, request.column);
            }
            app.open_requests.clear();
            if (leaf != rv_editor_tile_none) {
                shell.ws.focused_leaf = leaf;
            }
        }
    }

    // What the tiles say this frame: a code tile's file and its unsaved mark in
    // the header, and each pane's minimum size (rv_editor_shell_minimums.cpp).
    shell.ws.titles.clear();
    shell.ws.minimums.clear();
    for (const rv_editor_tile_node &node : shell.ws.layout.nodes) {
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id pane : node.leaf.tabs) {
            const rv_editor_pane_kind kind = shell.ws.panes.panes[pane].kind;
            rv_editor_shell_set_minimum(shell, pane, kind, app.game_need);
            rv_editor_shell_title(shell, pane, kind);
        }
    }

    // Files follows the document in the focused code tile.
    const rv_editor_workspace &ws = shell.ws;
    if (app.files.is_open() && ws.focused_leaf < ws.layout.nodes.size() &&
        ws.layout.nodes[ws.focused_leaf].kind == rv_editor_tile_kind::leaf) {
        const rv_editor_tile_leaf &leaf = ws.layout.nodes[ws.focused_leaf].leaf;
        if (!leaf.tabs.empty() && ws.panes.panes[leaf.tabs[leaf.active]].kind == rv_editor_pane_kind::code) {
            const rv_editor_nvim_buffer *buf = app.nvim.buffer_in(app.nvim.window_for(leaf.tabs[leaf.active]));
            if (buf != nullptr && !buf->name.empty() && buf->name != shell.revealed) {
                shell.revealed = buf->name;
                app.files.reveal(buf->name);
            }
        }
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
    const bool capture_lost = new_session || !app.game_drawn || !shell.window_focused || !app.session.live();
    if (capture_lost) {
        shell.ui.game_captured = false;
    }
    // The shell owns the Game's frame state and resets it itself: a pane only reports the draw.
    app.game_drawn = false;
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
