// The window's menus, shortcuts and pane dispatch.

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

// Where a file the user opened goes: the focused tile's code pane, any code
// pane, or a new one in the focused tile.
rv_editor_pane_id rv_editor_code_target(rv_editor_workspace &ws)
{
    if (ws.focused_leaf < ws.layout.nodes.size() && ws.layout.nodes[ws.focused_leaf].kind == rv_editor_tile_kind::leaf) {
        const rv_editor_tile_leaf &leaf = ws.layout.nodes[ws.focused_leaf].leaf;
        if (!leaf.tabs.empty() && ws.panes.panes[leaf.tabs[leaf.active]].kind == rv_editor_pane_kind::code) {
            return leaf.tabs[leaf.active];
        }
    }
    const std::vector<rv_editor_pane_id> code = rv_editor_code_panes(ws);
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

// A menu item that is disabled with its reason shown on hover (UI-04).
bool rv_editor_menu_item(const char *label, const char *shortcut, const char *why_not)
{
    const bool clicked = ImGui::MenuItem(label, shortcut, false, why_not == nullptr);
    if (why_not != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", why_not);
    }
    return clicked;
}

// Brings a pane of `kind` to the front where the tree already shows one, or adds
// one as a tab of the focused tile.
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
    const bool focused =
        ws.focused_leaf < ws.layout.nodes.size() && ws.layout.nodes[ws.focused_leaf].kind == rv_editor_tile_kind::leaf;
    const uint32_t leaf = focused ? ws.focused_leaf : ws.layout.root;
    const rv_editor_pane_id pane = rv_editor_pane_add(ws.panes, kind);
    rv_editor_tile_insert(ws.layout, leaf, pane, rv_editor_tile_dock::tab);
    rv_editor_tile_activate(ws.layout, pane);
}

} // namespace

rv_editor_workspace rv_editor_workspace_preset(rv_editor_layout_preset preset)
{
    rv_editor_workspace ws;
    rv_editor_layout_preset_make(preset, ws.panes, ws.layout);
    return ws;
}

rv_editor_workspace rv_editor_workspace_load(const std::filesystem::path &path)
{
    rv_editor_workspace ws = rv_editor_workspace_preset(rv_editor_layout_preset::workspace);
    if (path.empty() || rv_editor_layout_load(path, ws.panes, ws.layout)) {
        return ws;
    }
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::fprintf(stderr, "3dmppc-editor: %s is not a layout this editor reads; starting from Code\n",
            path.c_str());
    }
    return ws;
}

void rv_editor_shell_menu(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    rv_editor_workspace &ws = shell.ws;
    rv_editor_menu_style_push();
    if (!ImGui::BeginMainMenuBar()) {
        rv_editor_menu_style_pop();
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Folder...")) {
            rv_editor_open_folder(shell);
        }
        if (ImGui::MenuItem("Open disc.toml...")) {
            rv_editor_open_manifest(shell);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save As...")) {
            rv_editor_shell_save_as_start(shell);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Run")) {
        if (rv_editor_menu_item("Build", "Ctrl+B", rv_editor_app_why_not_build(app))) {
            rv_editor_app_build(app);
        }
        const char *why_not_cancel = app.build.state() == rv_editor_build_state::building ? nullptr : "No build to cancel";
        if (rv_editor_menu_item("Cancel Build", nullptr, why_not_cancel)) {
            app.build.cancel();
        }
        ImGui::Separator();
        const bool paused = app.session.state() == rv_editor_run_state::paused;
        if (rv_editor_menu_item(paused ? "Resume" : "Run", "F5", rv_editor_app_why_not_run(app))) {
            rv_editor_app_run(app);
        }
        if (rv_editor_menu_item("Run Last Successful Build", nullptr, rv_editor_app_why_not_run_last(app))) {
            rv_editor_app_run_last(app);
        }
        if (rv_editor_menu_item("Pause", "F6", rv_editor_app_why_not_pause(app))) {
            rv_editor_app_pause(app);
        }
        if (rv_editor_menu_item("Step Frame", "F7", rv_editor_app_why_not_step(app))) {
            rv_editor_app_step(app);
        }
        if (rv_editor_menu_item("Stop", "Shift+F5", rv_editor_app_why_not_stop(app))) {
            rv_editor_app_stop(app);
        }
        if (rv_editor_menu_item("Force Stop", nullptr, app.session.live() ? nullptr : "No runtime is running")) {
            app.session.force_stop(app.log);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::BeginMenu("Code Text Size")) {
            constexpr struct
            {
                rv_editor_code_size size;
                const char *label;
            } sizes[] = { { rv_editor_code_size::small, "Small (6x11)" }, { rv_editor_code_size::normal, "Normal (9x16)" },
                { rv_editor_code_size::large, "Large (9x16, doubled)" } };
            for (const auto &s : sizes) {
                if (ImGui::MenuItem(s.label, nullptr, rv_editor_font_code_size() == s.size)) {
                    rv_editor_font_code_size_set(s.size);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Game Scale")) {
            constexpr struct
            {
                rv_editor_game_scale scale;
                const char *label;
            } scales[] = { { rv_editor_game_scale::fit, "Fit" }, { rv_editor_game_scale::integer, "Integer" },
                { rv_editor_game_scale::x1, "1x" }, { rv_editor_game_scale::x2, "2x" }, { rv_editor_game_scale::x3, "3x" } };
            for (const auto &s : scales) {
                if (ImGui::MenuItem(s.label, nullptr, app.game_scale == s.scale)) {
                    app.game_scale = s.scale;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Layout")) {
        if (ImGui::MenuItem("Reset to Default")) {
            ws = rv_editor_workspace_preset(rv_editor_layout_preset::workspace);
        }
        // The design references' layouts hold panes that are not written yet.
        if (ImGui::BeginMenu("Reference Layouts")) {
            constexpr rv_editor_layout_preset presets[] = { rv_editor_layout_preset::code,
                rv_editor_layout_preset::scene, rv_editor_layout_preset::debug, rv_editor_layout_preset::build };
            for (const rv_editor_layout_preset preset : presets) {
                if (ImGui::MenuItem(rv_editor_layout_preset_name(preset))) {
                    ws = rv_editor_workspace_preset(preset);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Project Settings")) {
            rv_editor_shell_show(ws, rv_editor_pane_kind::project);
        }
        rv_editor_menu_item("Run Configuration", nullptr, "Run Configuration is not written yet");
        ImGui::Separator();
        if (ImGui::MenuItem("Widget Catalog")) {
            rv_editor_shell_show(ws, rv_editor_pane_kind::catalog);
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    rv_editor_menu_style_pop();
}

void rv_editor_shell_shortcuts(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    // A text field keeps its own keys; F-keys are never text.
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_B)) {
        rv_editor_app_build(app);
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F5)) {
        rv_editor_app_run(app);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_F5)) {
        rv_editor_app_stop(app);
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F6)) {
        rv_editor_app_pause(app);
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F7)) {
        rv_editor_app_step(app);
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


    // A code pane that left the tree, whatever took it (close, another kind, a
    // layout from the menu), gives its nvim window back.
    const std::vector<rv_editor_pane_id> shown = rv_editor_code_panes(shell.ws);
    for (const uint32_t pane : shell.app.nvim.panes()) {
        if (std::find(shown.begin(), shown.end(), pane) == shown.end()) {
            shell.app.nvim.release(pane);
        }
    }

    // Files the user opened go to a code tile once its window exists.
    rv_editor_app &app = shell.app;
    if (!app.open_requests.empty() && !app.nvim.problem.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
            "cannot open " + app.open_requests.front().string() + ": " + app.nvim.problem);
        app.open_requests.clear();
    }
    if (!app.open_requests.empty()) {
        const rv_editor_pane_id pane = rv_editor_code_target(shell.ws);
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
        case rv_editor_pane_kind::controls: rv_editor_pane_controls(shell.app, theme); return;
        case rv_editor_pane_kind::output:
        case rv_editor_pane_kind::console: rv_editor_pane_output(shell.app, pane, theme); return;
        case rv_editor_pane_kind::project: rv_editor_pane_project(shell.app, theme); return;
        case rv_editor_pane_kind::files: rv_editor_pane_files(shell.app, pane, theme); return;
        case rv_editor_pane_kind::code: rv_editor_pane_code(shell.app, pane, theme); return;
        case rv_editor_pane_kind::game: rv_editor_pane_game(shell.app, shell.renderer, theme); return;
        default: break;
    }
    rv_editor_note(std::string(rv_editor_pane_title(kind)) + ": not implemented yet.");
}

} // namespace rv_editor
