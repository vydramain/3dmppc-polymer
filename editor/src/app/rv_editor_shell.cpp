// The window's models loop and pane dispatch.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <system_error>

#include "imgui.h"

#include "catalog/rv_editor_catalog.hpp"
#include "font/rv_editor_font.hpp"
#include "panes/rv_editor_panes.hpp"

namespace rv_editor
{

namespace
{

// A dimmed note that wraps at the pane's edge instead of running under it (UI-05).
void rv_editor_note(const std::string &text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

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
        rv_editor_tile_activate(ws.layout, last);
        return last;
    }
    if (!code.empty()) {
        rv_editor_tile_activate(ws.layout, code.front());
        return code.front();
    }
    const bool focused =
        ws.focused_leaf < ws.layout.nodes.size() && ws.layout.nodes[ws.focused_leaf].kind == rv_editor_tile_kind::leaf;
    const rv_editor_pane_id pane = rv_editor_pane_add(ws.panes, rv_editor_pane_kind::code);
    rv_editor_tile_insert(ws.layout, focused ? ws.focused_leaf : ws.layout.root, pane, rv_editor_tile_dock::tab);
    rv_editor_tile_activate(ws.layout, pane);
    return pane;
}

// The leaf a new pane goes to: the focused one, else the maximized one, else the
// first leaf of the tree, so a menu command works with no tile focused (CAT-01).
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
                rv_editor_tile_activate(ws.layout, pane);
                return pane;
            }
        }
    }
    const rv_editor_pane_id pane = rv_editor_pane_add(ws.panes, kind);
    rv_editor_tile_insert(ws.layout, roomy ? rv_editor_roomy_leaf(ws) : rv_editor_target_leaf(ws), pane,
        rv_editor_tile_dock::tab);
    rv_editor_tile_activate(ws.layout, pane);
    return pane;
}

// A pane's title with its context (spec 6.1): what it shows, from which session,
// build or candidate; nothing when the plain kind says it all.
void rv_editor_shell_title(rv_editor_shell &shell, rv_editor_pane_id pane, rv_editor_pane_kind kind)
{
    rv_editor_app &app = shell.app;
    const bool burn = shell.active == rv_editor_layout_preset::burn;
    const rv_editor_session &s = app.session;
    const std::string session = s.number() == 0 ? std::string()
                                                 : "session " + std::to_string(s.number()) + " (" +
            rv_editor_run_state_name(s.state()) + ")";
    const std::string candidate = app.release.candidates.empty()
        ? std::string()
        : "#" + std::to_string(app.release.candidates[app.release.selected].number);
    std::string title;
    if (kind == rv_editor_pane_kind::game) {
        title = burn ? "Candidate Playtest" + (candidate.empty() ? "" : ": " + candidate)
                     : "Game" + (session.empty() ? "" : ": " + session);
    } else if (kind == rv_editor_pane_kind::runtime_log) {
        title = burn ? "Playtest Log" + (candidate.empty() ? "" : ": " + candidate)
                     : "Runtime Log" + (session.empty() ? "" : ": " + session);
    } else if (kind == rv_editor_pane_kind::scene && app.scene != nullptr) {
        title = "Scene: " + rv_editor_app_scene_name(app) + (app.scene->dirty ? " *" : "") +
            (app.scene->scene.read_only.empty() ? "" : " (read-only)");
    } else if (kind == rv_editor_pane_kind::problems && !app.problems.empty()) {
        title = "Problems (" + std::to_string(app.problems.size()) + ")";
    } else if (kind == rv_editor_pane_kind::output) {
        title = rv_editor_output_title(app, pane);
    } else if (kind == rv_editor_pane_kind::build_log && app.build.number() != 0) {
        title = "Build Log: build " + std::to_string(app.build.number());
    } else if (kind == rv_editor_pane_kind::terminal) {
        const auto it = app.terminals.find(pane);
        if (it != app.terminals.end() && it->second.term != nullptr) {
            title = it->second.term->running() ? "Terminal: " + it->second.cwd.filename().string()
                                               : "Terminal: ended (" + it->second.term->ended() + ")";
        }
    } else if (kind == rv_editor_pane_kind::code && app.nvim.running()) {
        const rv_editor_nvim_buffer *buf = app.nvim.buffer_in(app.nvim.window_for(pane));
        if (buf != nullptr) {
            title = "Code: " + rv_editor_shell_buffer_label(app, buf->name) + (buf->modified ? " *" : "");
        }
    }
    if (!title.empty()) {
        shell.ws.titles[pane] = title;
    }
}

} // namespace

void rv_editor_shell_page(rv_editor_shell &shell, rv_editor_start_page page)
{
    if (!shell.app.project.open) {
        shell.start_page = page;
        return;
    }
    switch (page) {
        case rv_editor_start_page::open_project: rv_editor_shell_show(shell.ws, rv_editor_pane_kind::open_project, true); return;
        case rv_editor_start_page::settings: rv_editor_shell_show(shell.ws, rv_editor_pane_kind::settings, true); return;
        case rv_editor_start_page::recent:
        case rv_editor_start_page::new_project: return;
    }
}

void rv_editor_shell_open_project(rv_editor_shell &shell)
{
    rv_editor_shell_page(shell, rv_editor_start_page::open_project);
}

void rv_editor_shell_show_pane(rv_editor_shell &shell, rv_editor_pane_kind kind)
{
    rv_editor_shell_show(shell.ws, kind);
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
        rv_editor_tile_set_ratio(ws.layout, ws.layout.nodes[leaf].parent, 0.5f);
        ws.focused_leaf = leaf;
    }
}

void rv_editor_shell_update(rv_editor_shell &shell)
{
    rv_editor_shell_fit_game(shell);
    rv_editor_app_update(shell.app);
    // Build or Run met unsaved files: the question opens, unless another is open.
    if (shell.app.unsaved_ask != rv_editor_unsaved_ask::none && shell.leaving == rv_editor_shell::rv_editor_leave::none) {
        shell.leaving = shell.app.unsaved_ask == rv_editor_unsaved_ask::build ? rv_editor_shell::rv_editor_leave::build
                                                                              : rv_editor_shell::rv_editor_leave::run;
    }
    shell.app.unsaved_ask = rv_editor_unsaved_ask::none;
    rv_editor_shell_after_save(shell);


    rv_editor_app &requests = shell.app;
    requests.preset = shell.active;
    requests.release_view = shell.active == rv_editor_layout_preset::burn;
    if (requests.show_request != rv_editor_pane_kind::empty) {
        rv_editor_shell_show(shell.ws, requests.show_request);
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

    // A Terminal tile that left every workspace's tree, or became another kind,
    // ends its shell.
    std::erase_if(shell.app.terminals, [&shell](const auto &entry) {
        const rv_editor_pane_id pane = entry.first;
        return pane >= shell.ws.panes.panes.size() ||
            shell.ws.panes.panes[pane].kind != rv_editor_pane_kind::terminal || !rv_editor_shell_pane_kept(shell, pane);
    });

    // Files the user opened go to a code tile once its window exists. A binary
    // one opens as text only on the user's word: nvim would show it as noise.
    rv_editor_app &app = shell.app;
    std::erase_if(app.open_requests, [&app](const std::pair<std::filesystem::path, int32_t> &request) {
        uintmax_t size = 0;
        if (app.open_as_text.erase(request.first) != 0 || !rv_editor_file_binary(request.first, size)) {
            return false;
        }
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
            rv_editor_shell_buffer_label(app, request.first.string()) + " is binary (" + std::to_string(size) +
                " bytes), not opened as text: Files > Open as Text shows it anyway");
        return true;
    });
    if (!app.open_requests.empty() && !app.nvim.problem.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
            "cannot open " + app.open_requests.front().first.string() + ": " + app.nvim.problem);
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
            for (const auto &[path, line] : app.open_requests) {
                app.nvim.open(win, path, line);
            }
            app.open_requests.clear();
            if (leaf != rv_editor_tile_none) {
                shell.ws.focused_leaf = leaf;
            }
        }
    }

    // What the tiles say this frame: a code tile's file and its unsaved mark in
    // the header, and the Game's frame at 1x as its minimum.
    shell.ws.titles.clear();
    shell.ws.minimums.clear();
    for (const rv_editor_tile_node &node : shell.ws.layout.nodes) {
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id pane : node.leaf.tabs) {
            const rv_editor_pane_kind kind = shell.ws.panes.panes[pane].kind;
            if (kind == rv_editor_pane_kind::game && app.game_need.w > 0) {
                shell.ws.minimums[pane] = app.game_need;
            }
            // A strip of controls is as tall as its rows, whatever its split's ratio says.
            const auto strip = shell.strips.find(pane);
            if (strip != shell.strips.end()) {
                shell.ws.minimums[pane] = strip->second;
            }
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
    // A Game tile closed or behind another tab, a window without the keyboard or
    // no console: the capture ends here, whatever the pane saw.
    if (!app.game_drawn || !shell.window_focused || !app.session.live()) {
        app.game_captured = false;
    }
    app.game_drawn = false;
    uint64_t buttons = 0;
    if (app.game_captured) {
        // ImGui's keyboard navigation stays off the next frame, so arrows stay the game's.
        app.text_focus = true;
        if (app.session.state() == rv_editor_run_state::running) {
            buttons = rv_editor_game_keys();
        }
    }
    // Only a change is sent; one the runtime cannot take yet goes again next frame.
    app.session.pad(buttons, app.log);
}

void rv_editor_shell_pane(void *context, rv_editor_pane_id pane, rv_editor_pane_kind kind, const rv_editor_theme &theme)
{
    rv_editor_shell &shell = *static_cast<rv_editor_shell *>(context);
    switch (kind) {
        case rv_editor_pane_kind::catalog: rv_editor_catalog_draw(theme); return;
        case rv_editor_pane_kind::candidate: rv_editor_pane_candidate(shell.app, theme); return;
        case rv_editor_pane_kind::checks: rv_editor_pane_checks(shell.app, theme); return;
        case rv_editor_pane_kind::scene: rv_editor_pane_scene(shell.app, theme); return;
        case rv_editor_pane_kind::hierarchy: rv_editor_pane_hierarchy(shell.app, theme); return;
        case rv_editor_pane_kind::assets: rv_editor_pane_assets(shell.app, shell.renderer, theme); return;
        case rv_editor_pane_kind::inspector: rv_editor_pane_scene_inspector(shell.app, theme); return;
        case rv_editor_pane_kind::build_result: rv_editor_pane_build_result(shell.app, theme); return;
        case rv_editor_pane_kind::problems: rv_editor_pane_problems(shell.app, theme); return;
        case rv_editor_pane_kind::search: rv_editor_pane_search(shell.app, theme); return;
        case rv_editor_pane_kind::run_config: rv_editor_pane_run_config(shell.app, theme); return;
        case rv_editor_pane_kind::controls:
        case rv_editor_pane_kind::release_controls:
        case rv_editor_pane_kind::toolchest: {
            // Measured each frame: a strip's minimum is what it drew.
            const float top = ImGui::GetCursorPosY();
            if (kind == rv_editor_pane_kind::controls) {
                rv_editor_pane_controls(shell.app, theme);
            } else if (kind == rv_editor_pane_kind::toolchest) {
                rv_editor_pane_toolchest(shell.app, theme);
            } else {
                rv_editor_pane_release_controls(shell.app, theme);
            }
            const float tall = ImGui::GetCursorPosY() - top - ImGui::GetStyle().ItemSpacing.y;
            shell.strips[pane] = { static_cast<int32_t>(ImGui::GetFontSize() * 20), static_cast<int32_t>(tall) };
            return;
        }
        case rv_editor_pane_kind::runtime_log:
        case rv_editor_pane_kind::build_log: {
            // One source each, until the user ticks more (TRM-02).
            const bool runtime = kind == rv_editor_pane_kind::runtime_log;
            rv_editor_output_view view;
            view.show = { false, !runtime, runtime, false };
            shell.app.outputs.try_emplace(pane, view);
            rv_editor_pane_output(shell.app, pane, theme);
            return;
        }
        case rv_editor_pane_kind::output:
        case rv_editor_pane_kind::console: rv_editor_pane_output(shell.app, pane, theme); return;
        case rv_editor_pane_kind::project: rv_editor_pane_project(shell.app, theme); return;
        case rv_editor_pane_kind::files: rv_editor_pane_files(shell.app, pane, theme); return;
        case rv_editor_pane_kind::code: rv_editor_pane_code(shell.app, pane, theme); return;
        case rv_editor_pane_kind::terminal: rv_editor_pane_terminal(shell.app, pane, theme); return;
        case rv_editor_pane_kind::game: rv_editor_pane_game(shell.app, shell.renderer, theme); return;
        case rv_editor_pane_kind::observe: rv_editor_pane_observe(shell.app, theme); return;
        case rv_editor_pane_kind::findings: rv_editor_pane_findings(shell.app, theme); return;
        case rv_editor_pane_kind::session: rv_editor_pane_session(shell.app, theme); return;
        case rv_editor_pane_kind::test_case: rv_editor_pane_test_case(shell.app, theme); return;
        case rv_editor_pane_kind::open_project: rv_editor_page_open_project(shell, theme); return;
        case rv_editor_pane_kind::settings: rv_editor_page_settings(shell, theme); return;
        default: break;
    }
    rv_editor_note(std::string(rv_editor_pane_title(kind)) + ": not implemented yet.");
}

} // namespace rv_editor
