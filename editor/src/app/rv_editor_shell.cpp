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

// SDL calls this when a dialog closes, maybe on another thread.
void SDLCALL rv_editor_dialog_done(void *userdata, const char *const *files, int)
{
    if (files == nullptr || files[0] == nullptr) {
        return; // cancelled or failed
    }
    rv_editor_shell &shell = *static_cast<rv_editor_shell *>(userdata);
    const std::lock_guard<std::mutex> lock(shell.picked_mutex);
    shell.picked.emplace_back(files[0]);
}

void rv_editor_open_folder(rv_editor_shell &shell)
{
    SDL_ShowOpenFolderDialog(rv_editor_dialog_done, &shell, shell.window, nullptr, false);
}

void rv_editor_open_manifest(rv_editor_shell &shell)
{
    static const SDL_DialogFileFilter filters[] = { { "Disc manifest (disc.toml)", "toml" } };
    SDL_ShowOpenFileDialog(rv_editor_dialog_done, &shell, shell.window, filters, 1, nullptr, false);
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

// Brings a pane of `kind` to the front where the tree already shows one, or adds
// one as a tab of the target leaf.
void rv_editor_shell_show(rv_editor_workspace &ws, rv_editor_pane_kind kind)
{
    for (const rv_editor_tile_node &node : ws.layout.nodes) {
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id pane : node.leaf.tabs) {
            if (ws.panes.panes[pane].kind == kind) {
                rv_editor_tile_activate(ws.layout, pane);
                return;
            }
        }
    }
    const rv_editor_pane_id pane = rv_editor_pane_add(ws.panes, kind);
    rv_editor_tile_insert(ws.layout, rv_editor_target_leaf(ws), pane, rv_editor_tile_dock::tab);
    rv_editor_tile_activate(ws.layout, pane);
}

} // namespace

void rv_editor_shell_open_folder(rv_editor_shell &shell)
{
    rv_editor_open_folder(shell);
}

void rv_editor_shell_open_manifest(rv_editor_shell &shell)
{
    rv_editor_open_manifest(shell);
}

void rv_editor_shell_show_pane(rv_editor_shell &shell, rv_editor_pane_kind kind)
{
    rv_editor_shell_show(shell.ws, kind);
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
    std::vector<std::filesystem::path> picked;
    {
        const std::lock_guard<std::mutex> lock(shell.picked_mutex);
        picked.swap(shell.picked);
    }
    for (const std::filesystem::path &path : picked) {
        rv_editor_shell_request_open(shell, path);
    }
    rv_editor_app_update(shell.app);
    rv_editor_shell_after_save(shell);


    rv_editor_app &requests = shell.app;
    requests.preset = shell.active;
    requests.release_view = shell.active == rv_editor_layout_preset::burn;
    if (shell.active == rv_editor_layout_preset::burn || shell.active == rv_editor_layout_preset::burn_diagnose) {
        shell.burn_last = shell.active;
    }
    if (requests.burn_submode_request != rv_editor_layout_preset::code) {
        rv_editor_shell_switch(shell, requests.burn_submode_request);
        requests.burn_submode_request = rv_editor_layout_preset::code;
    }
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
        rv_editor_open_folder(shell);
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

    // Files the user opened go to a code tile once its window exists.
    rv_editor_app &app = shell.app;
    if (!app.open_requests.empty() && !app.nvim.problem.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
            "cannot open " + app.open_requests.front().string() + ": " + app.nvim.problem);
        app.open_requests.clear();
    }
    if (!app.open_requests.empty()) {
        const rv_editor_pane_id pane = rv_editor_code_target(shell.ws, shell.last_code);
        const int64_t win = app.nvim.window_for(pane);
        if (win != 0) {
            for (const std::filesystem::path &path : app.open_requests) {
                app.nvim.open(win, path, 0);
            }
            app.open_requests.clear();
            const uint32_t leaf = rv_editor_tile_find(shell.ws.layout, pane);
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
            if (shell.active == rv_editor_layout_preset::burn && kind == rv_editor_pane_kind::game) {
                shell.ws.titles[pane] = "Candidate Playtest";
            }
            if (shell.active == rv_editor_layout_preset::burn && kind == rv_editor_pane_kind::runtime_log) {
                shell.ws.titles[pane] = "Playtest Log";
            }
            if (kind != rv_editor_pane_kind::code || !app.nvim.running()) {
                continue;
            }
            const rv_editor_nvim_buffer *buf = app.nvim.buffer_in(app.nvim.window_for(pane));
            if (buf != nullptr) {
                shell.ws.titles[pane] =
                    "Code - " + rv_editor_shell_buffer_label(app, buf->name) + (buf->modified ? " [+]" : "");
            }
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
        case rv_editor_pane_kind::build_result: rv_editor_pane_build_result(shell.app, theme); return;
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
        default: break;
    }
    rv_editor_note(std::string(rv_editor_pane_title(kind)) + ": not implemented yet.");
}

} // namespace rv_editor
