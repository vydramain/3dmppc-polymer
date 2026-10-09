// The shell's frame: the steps one frame runs, in order.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/rv_editor_shell.hpp"

namespace rv_editor
{

namespace
{

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

// The active tab of the focused tile when it is a code pane; rv_editor_tile_none otherwise.
rv_editor_pane_id rv_editor_focused_code_pane(const rv_editor_workspace &ws)
{
    if (ws.focused_leaf >= ws.layout.nodes.size() || ws.layout.nodes[ws.focused_leaf].kind != rv_editor_tile_kind::leaf) {
        return rv_editor_tile_none;
    }
    const rv_editor_tile_leaf &leaf = ws.layout.nodes[ws.focused_leaf].leaf;
    if (leaf.tabs.empty() || ws.panes.panes[leaf.tabs[leaf.active]].kind != rv_editor_pane_kind::code) {
        return rv_editor_tile_none;
    }
    return leaf.tabs[leaf.active];
}

// Where a file the user opened goes: the focused tile's code pane, the code
// pane used last, any code pane, or a new one in the focused tile.
rv_editor_pane_id rv_editor_code_target(rv_editor_workspace &ws, rv_editor_pane_id last)
{
    const rv_editor_pane_id focused = rv_editor_focused_code_pane(ws);
    if (focused != rv_editor_tile_none) {
        return focused;
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
    const bool focused_leaf =
        ws.focused_leaf < ws.layout.nodes.size() && ws.layout.nodes[ws.focused_leaf].kind == rv_editor_tile_kind::leaf;
    const rv_editor_pane_id pane = rv_editor_pane_add(ws.panes, rv_editor_pane_kind::code);
    rv_editor_tile_insert(ws.layout, focused_leaf ? ws.focused_leaf : ws.layout.root, pane, rv_editor_tile_dock::tab);
    (void)rv_editor_tile_activate(ws.layout, pane);
    return pane;
}

// A closed tile's shell is hung up here and ended by the frame steps; nothing waits.
void rv_editor_shell_stop_terminal(rv_editor_app &app, rv_editor_terminal_view &view)
{
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

// Forgets the ask once the frame has handled it; a field clear, cannot fail.
void rv_editor_shell_clear_unsaved_ask(rv_editor_shell &shell)
{
    shell.app.unsaved_ask = rv_editor_unsaved_ask::none;
}

// What the window's frame passes to the panes: the layout, and whether Burn releases its views. Cannot fail.
void rv_editor_shell_publish_layout(rv_editor_shell &shell)
{
    shell.app.preset = shell.active;
    shell.app.release_view = shell.active == rv_editor_layout_preset::burn;
}

// Run Configuration's list and form need room: the biggest tile takes it. Cannot fail.
void rv_editor_shell_run_show_request(rv_editor_shell &shell)
{
    rv_editor_shell_show_pane(shell, shell.app.show_request, shell.app.show_request == rv_editor_pane_kind::run_config);
    shell.app.show_request = rv_editor_pane_kind::empty;
}

// Cannot fail.
void rv_editor_shell_take_files_select(rv_editor_shell &shell)
{
    shell.ui.files_selected = *shell.app.files_select;
    shell.app.files_select.reset();
}

// The flag is cleared, then the new-buffer keys go to the code window. With no window the menu step does
// nothing and logs nothing; this wrapper cannot fail.
void rv_editor_shell_run_new_file_request(rv_editor_shell &shell)
{
    shell.app.new_file_request = false;
    rv_editor_shell_new_file(shell);
}

// The flag is cleared, then the Open Project page is shown; the page step cannot fail and logs nothing.
void rv_editor_shell_run_open_folder_request(rv_editor_shell &shell)
{
    shell.app.open_folder_request = false;
    rv_editor_shell_open_project(shell);
}

// Cannot fail. A code pane that left every workspace's tree, whatever took it (close,
// another kind, a layout from the menu), gives its nvim window back; one in a
// workspace not shown keeps it.
void rv_editor_shell_release_windows(rv_editor_shell &shell)
{
    for (const uint32_t pane : shell.app.nvim.panes()) {
        const bool left =
            shell.ws.panes.panes[pane].kind != rv_editor_pane_kind::code || !rv_editor_shell_pane_kept(shell, pane);
        if (left) {
            shell.app.nvim.release(pane);
            shell.app.code_tabs.erase(pane);
        }
    }
}

// Cannot fail: the code tile in front of the focused tile is the one used last.
void rv_editor_shell_set_last_code(rv_editor_shell &shell, rv_editor_pane_id pane)
{
    shell.last_code = pane;
}

// Cannot fail: the first code tile, or none, is the one used last.
void rv_editor_shell_first_code(rv_editor_shell &shell)
{
    const std::vector<rv_editor_pane_id> code = rv_editor_code_panes(shell.ws);
    shell.last_code = code.empty() ? rv_editor_tile_none : code.front();
}

// last_code's buffer, once its tile holds a window; nullptr otherwise.
const rv_editor_nvim_buffer *rv_editor_shell_last_code_buffer(rv_editor_shell &shell)
{
    const auto held = shell.app.nvim.panes();
    if (std::find(held.begin(), held.end(), shell.last_code) == held.end()) {
        return nullptr;
    }
    return shell.app.nvim.buffer_in(shell.app.nvim.window_for(shell.last_code));
}

// last_code's file, for Reload; empty unless the tile already has a window. Cannot fail.
void rv_editor_shell_code_file(rv_editor_shell &shell)
{
    shell.app.code_file.clear();
    const rv_editor_nvim_buffer *buf = rv_editor_shell_last_code_buffer(shell);
    const bool named = buf != nullptr && !buf->name.empty();
    if (named) {
        shell.app.code_file = buf->name;
    }
}

// A Terminal tile that left every workspace's tree, or became another kind, ends its shell.
// Cannot fail: each shell's refusal is logged by the stop.
void rv_editor_shell_end_unkept_terminals(rv_editor_shell &shell)
{
    for (auto it = shell.app.terminals.begin(); it != shell.app.terminals.end();) {
        const rv_editor_pane_id pane = it->first;
        const bool kept = pane < shell.ws.panes.panes.size() &&
            shell.ws.panes.panes[pane].kind == rv_editor_pane_kind::terminal && rv_editor_shell_pane_kept(shell, pane);
        if (kept) {
            ++it;
            continue;
        }
        const bool has_shell = it->second.term != nullptr;
        if (has_shell) {
            rv_editor_shell_stop_terminal(shell.app, it->second);
        }
        it = shell.app.terminals.erase(it);
    }
}

// Files the user opened go to a code tile once its window exists. A binary one opens as text
// only on the user's word: nvim would show it as noise. Cannot fail: each refusal is logged.
void rv_editor_shell_refuse_binary_requests(rv_editor_app &app)
{
    std::erase_if(app.open_requests, [&app](const rv_editor_open_request &request) {
        const bool as_text = app.open_as_text.erase(request.path) != 0;
        uintmax_t size = 0;
        const bool binary = !as_text && rv_editor_file_binary(request.path, size);
        if (!binary) {
            return false;
        }
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            rv_editor_shell_buffer_label(app, request.path.string()) + " is binary (" + std::to_string(size) +
                " bytes), not opened as text: Files > Open as Text shows it anyway");
        return true;
    });
}

// The nvim session has a problem: the first request's refusal is logged, and all are dropped. Cannot fail.
void rv_editor_shell_refuse_open_requests(rv_editor_app &app)
{
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::error,
        "cannot open " + app.open_requests.front().path.string() + ": " + app.nvim.problem);
    app.open_requests.clear();
}

// A maximized tile that hides the code tile steps back, or its window never draws. Cannot fail.
void rv_editor_shell_unmaximize_hiding(rv_editor_shell &shell, uint32_t leaf)
{
    const uint32_t maximized = shell.ws.layout.maximized_leaf;
    const bool hides = maximized != rv_editor_tile_none && maximized != leaf;
    if (hides) {
        shell.ws.layout.maximized_leaf = rv_editor_tile_none;
    }
}

// Opens every request in the window, then drops them. Cannot fail.
void rv_editor_shell_send_open_requests(rv_editor_app &app, int64_t win)
{
    for (const auto &request : app.open_requests) {
        app.nvim.open(win, request.path, request.line, request.column);
    }
    app.open_requests.clear();
}

// The code tile the open requests go to, shown: a maximized tile hiding it steps back, or its window never
// draws. Cannot fail.
rv_editor_pane_id rv_editor_shell_open_target(rv_editor_shell &shell)
{
    const rv_editor_pane_id pane = rv_editor_code_target(shell.ws, shell.last_code);
    rv_editor_shell_unmaximize_hiding(shell, rv_editor_tile_find(shell.ws.layout, pane));
    return pane;
}

// The code tile takes the focus once its requests went to its window. Cannot fail.
void rv_editor_shell_focus_open_target(rv_editor_shell &shell, rv_editor_pane_id pane)
{
    const uint32_t leaf = rv_editor_tile_find(shell.ws.layout, pane);
    shell.ws.focused_leaf = leaf != rv_editor_tile_none ? leaf : shell.ws.focused_leaf;
}

// Forgets last frame's titles and minimum sizes; the tiles say them again. Cannot fail.
void rv_editor_shell_clear_marks(rv_editor_shell &shell)
{
    shell.ws.titles.clear();
    shell.ws.minimums.clear();
}

// What the tiles say this frame: a code tile's file and its unsaved mark in
// the header, and each pane's minimum size (rv_editor_shell_minimums.cpp). Cannot fail.
void rv_editor_shell_mark_panes(rv_editor_shell &shell)
{
    for (const rv_editor_tile_node &node : shell.ws.layout.nodes) {
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id pane : node.leaf.tabs) {
            const rv_editor_pane_kind kind = shell.ws.panes.panes[pane].kind;
            rv_editor_shell_set_minimum(shell, pane, kind, shell.ui.game_need);
            rv_editor_shell_title(shell, pane, kind);
        }
    }
}

// The buffer of the focused code tile, once its window exists; nullptr otherwise.
const rv_editor_nvim_buffer *rv_editor_shell_focused_buffer(rv_editor_shell &shell)
{
    const rv_editor_pane_id pane = rv_editor_focused_code_pane(shell.ws);
    if (pane == rv_editor_tile_none) {
        return nullptr;
    }
    return shell.app.nvim.buffer_in(shell.app.nvim.window_for(pane));
}

// The document's file, so Files follows it once per document change. Cannot fail.
void rv_editor_shell_set_revealed(rv_editor_shell &shell, const std::string &name)
{
    shell.revealed = name;
}

// NOENT is a miss: inside() accepted the path, but reveal() did not find it in the tree (skipped, gone, or
// a path form reveal does not match). Logged once per document change, as the revealed gate allows.
void rv_editor_shell_reveal_file(rv_editor_shell &shell, const std::string &name)
{
    const int revealed = shell.app.files.reveal(name);
    const bool found = revealed == RV_OK;
    if (found) {
        shell.ui.files_selected = name;
    }
    const bool missed = !found;
    if (missed) {
        shell.app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "not in Files tree: " + name);
    }
}

} // namespace

// The frame is not an operation: it runs the steps of one frame in order. The steps that log their own
// refusals: the unsaved-files question, the binary and nvim-problem refusals of the open requests, and the
// terminal stop, and the file reveal's miss. The code of the unsaved-files question is dropped with (void); a
// file outside the project root is not revealed by design.
void rv_editor_shell_update(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    rv_editor_shell_frame_start(shell);
    rv_editor_app_update(app);
    // Build, Run or Build and Restart met unsaved files: the question opens, or its refusal is logged.
    const bool asked = app.unsaved_ask != rv_editor_unsaved_ask::none;
    if (asked) {
        (void)rv_editor_shell_open_question(shell, app.unsaved_ask);
    }
    rv_editor_shell_clear_unsaved_ask(shell);
    rv_editor_shell_after_save(shell);
    rv_editor_shell_review_tab(shell);
    rv_editor_shell_settings_apply(shell);
    rv_editor_shell_publish_layout(shell);
    const bool show_asked = app.show_request != rv_editor_pane_kind::empty;
    if (show_asked) {
        rv_editor_shell_run_show_request(shell);
    }
    const bool select_asked = app.files_select.has_value();
    if (select_asked) {
        rv_editor_shell_take_files_select(shell);
    }
    const bool new_file_asked = app.new_file_request;
    if (new_file_asked) {
        rv_editor_shell_run_new_file_request(shell);
    }
    const bool folder_asked = app.open_folder_request;
    if (folder_asked) {
        rv_editor_shell_run_open_folder_request(shell);
    }
    rv_editor_shell_release_windows(shell);
    // The code tile in front of the focused tile is the one used last.
    const rv_editor_pane_id focused_code = rv_editor_focused_code_pane(shell.ws);
    const bool focused_is_code = focused_code != rv_editor_tile_none;
    if (focused_is_code) {
        rv_editor_shell_set_last_code(shell, focused_code);
    }
    // Until one is used, the first code tile is the one Save, Undo and Redo act on.
    const bool last_lost = shell.last_code == rv_editor_tile_none || !rv_editor_shell_pane_kept(shell, shell.last_code);
    if (last_lost) {
        rv_editor_shell_first_code(shell);
    }
    rv_editor_shell_code_file(shell);
    rv_editor_shell_end_unkept_terminals(shell);
    rv_editor_shell_refuse_binary_requests(app);
    const bool nvim_refused = !app.open_requests.empty() && !app.nvim.problem.empty();
    if (nvim_refused) {
        rv_editor_shell_refuse_open_requests(app);
    }
    // A wait for the window is not a failure: the requests stay queued and go out on the frame it exists.
    const bool opening = !app.open_requests.empty();
    const rv_editor_pane_id target = opening ? rv_editor_shell_open_target(shell) : rv_editor_tile_none;
    const int64_t win = opening ? app.nvim.window_for(target) : 0;
    const bool ready = win != 0;
    if (ready) {
        rv_editor_shell_send_open_requests(app, win);
        rv_editor_shell_focus_open_target(shell, target);
    }
    rv_editor_shell_clear_marks(shell);
    rv_editor_shell_mark_panes(shell);
    // Files follows the document in the focused code tile, once per document change. A file outside the
    // project root is not in Files, so it is marked as the document but not revealed.
    const rv_editor_nvim_buffer *focused = app.files.is_open() ? rv_editor_shell_focused_buffer(shell) : nullptr;
    const bool follows = focused != nullptr && !focused->name.empty() && focused->name != shell.revealed;
    if (follows) {
        rv_editor_shell_set_revealed(shell, focused->name);
    }
    const bool inside = follows && app.files.inside(focused->name);
    if (inside) {
        rv_editor_shell_reveal_file(shell, focused->name);
    }
}

} // namespace rv_editor
