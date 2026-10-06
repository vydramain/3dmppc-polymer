// The window's menu bar: File, Edit, View, Project, Run, Window,
// Help, the layout switch at its right end, and the shortcuts behind them.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

bool rv_editor_menu_item(const char *label, const char *shortcut, const char *why_not)
{
    const bool clicked = ImGui::MenuItem(label, shortcut, false, why_not == nullptr);
    if (why_not != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", why_not);
    }
    return clicked;
}

namespace
{

// A top-level menu with its mnemonic, the first letter, underlined. Alt
// moves the keyboard into the bar; the arrows walk it.
bool rv_editor_menu_begin(const char *label)
{
    const bool open = ImGui::BeginMenu(label);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float text = ImGui::CalcTextSize(label).x;
    const float x = std::floor(min.x + (max.x - min.x - text) / 2.0f);
    const char first[2] = { label[0], '\0' };
    // Below the baseline; the bar's own clip would cut it, so it goes on top.
    const float y = std::floor(min.y + (max.y - min.y + ImGui::GetFontSize()) / 2.0f) - 1.0f;
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + ImGui::CalcTextSize(first).x, y + 1.0f),
        ImGui::GetColorU32(ImGuiCol_Text));
    return open;
}

// The code tile used last, and its nvim window, for Save, Undo and Redo; 0 when
// there is none.
int64_t rv_editor_menu_code_window(rv_editor_shell &shell)
{
    if (shell.last_code == rv_editor_tile_none || !shell.app.nvim.running()) {
        return 0;
    }
    return shell.app.nvim.window_for(shell.last_code);
}

// Keys to the code tile used last, as if typed there: the editor's nvim config
// maps them (editor/nvim/rv_editor_init.lua).
void rv_editor_menu_code_keys(rv_editor_shell &shell, const char *keys)
{
    const int64_t win = rv_editor_menu_code_window(shell);
    if (win != 0) {
        shell.app.nvim.focus(win);
        shell.app.nvim.input(keys);
    }
}

void rv_editor_menu_file(rv_editor_shell &shell)
{
    const char *no_code = rv_editor_menu_code_window(shell) == 0 ? rv_editor_text("shell_menu.why_no_code_tile") : nullptr;
    if (rv_editor_menu_item(rv_editor_text("shell_menu.new_file"), nullptr, no_code)) {
        rv_editor_menu_code_keys(shell, "<Cmd>enew<CR>");
    }
    if (ImGui::MenuItem(rv_editor_text("shell_start.open_project"))) {
        rv_editor_shell_open_project(shell);
    }
    ImGui::Separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.save"), rv_editor_text("shell_menu.shortcut_save"), no_code)) {
        rv_editor_menu_code_keys(shell, "<C-s>");
    }
    if (ImGui::MenuItem(rv_editor_text("catalog_status.menu_save_all"), rv_editor_text("catalog_status.shortcut_save_all"))) {
        rv_editor_shell_save_all(shell);
    }
    if (ImGui::MenuItem(rv_editor_text("shell_menu.save_as"))) {
        rv_editor_shell_save_as_start(shell);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(rv_editor_text("shell_menu.settings"))) {
        rv_editor_shell_page(shell, rv_editor_start_page::settings);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(rv_editor_text("shell_menu.quit"))) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
}

void rv_editor_menu_edit(rv_editor_shell &shell)
{
    const char *no_code = rv_editor_menu_code_window(shell) == 0 ? rv_editor_text("shell_menu.why_no_code_edit") : nullptr;
    if (rv_editor_menu_item(rv_editor_text("shell_menu.undo"), rv_editor_text("shell_menu.shortcut_undo"), no_code)) {
        rv_editor_menu_code_keys(shell, "<C-z>");
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.redo"), rv_editor_text("shell_menu.shortcut_redo"), no_code)) {
        rv_editor_menu_code_keys(shell, "<C-S-z>");
    }
    ImGui::Separator();
    if (ImGui::MenuItem(rv_editor_text("shell_menu.find_in_project"), rv_editor_text("shell_menu.shortcut_find"))) {
        shell.app.project_search.focus = true;
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::search);
    }
}

void rv_editor_menu_view(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    if (ImGui::BeginMenu(rv_editor_text("shell_menu.code_text_size"))) {
        struct {
            rv_editor_code_size size;
            const char *text_key;
        } sizes[] = { { rv_editor_code_size::small, "shell_menu.size_small" },
            { rv_editor_code_size::normal, "shell_menu.size_normal" },
            { rv_editor_code_size::large, "shell_menu.size_large" } };
        for (const auto &s : sizes) {
            if (ImGui::MenuItem(rv_editor_text(s.text_key), nullptr, rv_editor_font_code_size() == s.size)) {
                rv_editor_font_code_size_set(s.size);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(rv_editor_text("shell_menu.game_scale"))) {
        struct {
            rv_editor_game_scale scale;
            const char *text_key;
        } scales[] = { { rv_editor_game_scale::fit, "pane_game.scale_fit" },
            { rv_editor_game_scale::integer, "pane_game.scale_integer" },
            { rv_editor_game_scale::x1, "pane_game.scale_1x" },
            { rv_editor_game_scale::x2, "pane_game.scale_2x" },
            { rv_editor_game_scale::x3, "pane_game.scale_3x" } };
        for (const auto &s : scales) {
            if (ImGui::MenuItem(rv_editor_text(s.text_key), nullptr, app.game_scale == s.scale)) {
                app.game_scale = s.scale;
            }
        }
        ImGui::EndMenu();
    }
    // Independent of the code text size and of the Game scale.
    if (ImGui::BeginMenu(rv_editor_text("shell_menu.ui_scale"))) {
        struct {
            float scale;
            const char *text_key;
        } scales[] = { { 1.0f, "shell_menu.ui_scale_1x" },
            { 1.5f, "shell_menu.ui_scale_15x" },
            { 2.0f, "shell_menu.ui_scale_2x" } };
        for (const auto &s : scales) {
            const bool fits = rv_editor_shell_scale_fits(shell.window, s.scale);
            const char *why_not = nullptr;
            std::string why_not_str;
            if (!fits && s.scale != 1.0f) {
                const int needed_w = static_cast<int>(std::ceil(static_cast<float>(window_min_width) * s.scale));
                const int needed_h = static_cast<int>(std::ceil(static_cast<float>(window_min_height) * s.scale));
                why_not_str = rv_editor_text_format(rv_editor_text("shell_menu.scale_needs_display"),
                    std::make_format_args(needed_w, needed_h));
                why_not = why_not_str.c_str();
            }
            const bool clicked =
                ImGui::MenuItem(rv_editor_text(s.text_key), nullptr, shell.ui_scale == s.scale, fits || s.scale == 1.0f);
            if (why_not != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", why_not);
            }
            if (clicked) {
                shell.ui_scale_request = s.scale;
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    // Independent of the focused tile and of a project.
    if (ImGui::MenuItem(rv_editor_text("shell_menu.widget_catalog"))) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::catalog);
    }
}

void rv_editor_menu_project(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    if (ImGui::MenuItem(rv_editor_text("shell_menu.project_settings"))) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::project);
    }
    ImGui::Separator();
    if (rv_editor_menu_item(rv_editor_text("widgets_status.button_build"), rv_editor_text("widgets_status.shortcut_build"),
            rv_editor_app_why_not_build(app))) {
        rv_editor_app_build(app);
    }
    const bool can_cancel = app.build.state() == rv_editor_build_state::building;
    const char *why_not_cancel = can_cancel ? nullptr : rv_editor_text("shell_menu.why_no_build");
    if (rv_editor_menu_item(rv_editor_text("shell_menu.cancel_build"), nullptr, why_not_cancel)) {
        app.build.cancel();
    }
}

void rv_editor_menu_run(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    const bool paused = app.session.state() == rv_editor_run_state::paused;
    const char *resume_text = rv_editor_text("widgets_status.button_resume");
    const char *run_text = rv_editor_text("widgets_status.button_run");
    const char *run_label = paused ? resume_text : run_text;
    const char *shortcut = rv_editor_text("widgets_status.shortcut_run");
    if (rv_editor_menu_item(run_label, shortcut, rv_editor_app_why_not_run(app))) {
        rv_editor_app_run(app);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.run_last"), nullptr, rv_editor_app_why_not_run_last(app))) {
        rv_editor_app_run_last(app);
    }
    if (rv_editor_menu_item(rv_editor_text("widgets_status.button_pause"), rv_editor_text("widgets_status.shortcut_pause"),
            rv_editor_app_why_not_pause(app))) {
        rv_editor_app_pause(app);
    }
    if (rv_editor_menu_item(rv_editor_text("widgets_status.button_step_frame"), rv_editor_text("widgets_status.shortcut_step"),
            rv_editor_app_why_not_step(app))) {
        rv_editor_app_step(app);
    }
    if (rv_editor_menu_item(rv_editor_text("widgets_status.button_stop"), rv_editor_text("widgets_status.shortcut_stop"),
            rv_editor_app_why_not_stop(app))) {
        rv_editor_app_stop(app);
    }
    const bool can_reload = app.session.live() && rv_editor_app_can_reload(app);
    const char *why_not_reload = can_reload ? rv_editor_app_why_not_reload(app) : rv_editor_text("shell_menu.why_no_reload");
    const rv_editor_change_plan reload_plan =
        can_reload ? rv_editor_app_change_for(app, app.code_file) : rv_editor_change_plan{};
    std::string reload_label;
    if (reload_plan.action == rv_editor_change_action::reload_module) {
        reload_label = rv_editor_text_format(rv_editor_text("shell_menu.reload_module"),
            std::make_format_args(reload_plan.name));
    } else if (reload_plan.action == rv_editor_change_action::refresh_texture) {
        reload_label = rv_editor_text_format(rv_editor_text("shell_menu.refresh_texture"),
            std::make_format_args(reload_plan.name));
    } else {
        reload_label = rv_editor_text("widgets_status.tooltip_reload");
    }
    if (rv_editor_menu_item(reload_label.c_str(), rv_editor_text("widgets_status.shortcut_reload"), why_not_reload)) {
        rv_editor_app_reload(app);
    }
    const char *why_not_build_restart =
        app.session.live() ? rv_editor_app_why_not_build(app) : rv_editor_text("shell_menu.why_no_game");
    if (rv_editor_menu_item(rv_editor_text("widgets_status.tooltip_restart"), nullptr, why_not_build_restart)) {
        rv_editor_app_build_restart(app);
    }
    ImGui::Separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.force_stop"), nullptr,
            app.session.live() ? nullptr : rv_editor_text("shell_menu.why_no_runtime"))) {
        app.session.force_stop(app.log);
    }
    ImGui::Separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.run_config"), nullptr,
            app.project.open ? nullptr : rv_editor_text("shell_menu.why_no_project"))) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::run_config, true);
    }
}

void rv_editor_menu_window(rv_editor_shell &shell)
{
    if (ImGui::MenuItem(rv_editor_text("shell_menu.terminal"))) {
        rv_editor_shell_focus_terminal(shell);
    }
    if (ImGui::MenuItem(rv_editor_text("shell_menu.focus_next_pane"), rv_editor_text("shell_menu.shortcut_focus_next"))) {
        rv_editor_shell_focus_next(shell, false);
    }
    if (ImGui::MenuItem(rv_editor_text("shell_menu.focus_prev_pane"), rv_editor_text("shell_menu.shortcut_focus_prev"))) {
        rv_editor_shell_focus_next(shell, true);
    }
    ImGui::Separator();
    // Every kind of tile, beside the focused one.
    if (ImGui::BeginMenu(rv_editor_text("shell_menu.new_tile"))) {
        for (uint32_t k = 1; k <= static_cast<uint32_t>(rv_editor_pane_kind_last); ++k) {
            const auto kind = static_cast<rv_editor_pane_kind>(k);
            if (ImGui::MenuItem(rv_editor_pane_title(kind))) {
                rv_editor_shell_new_tile(shell, kind);
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    // The same four the bar's right end switches between; each keeps its tiles.
    if (ImGui::BeginMenu(rv_editor_text("shell_menu.reference_layouts"))) {
        for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
            if (ImGui::MenuItem(rv_editor_layout_preset_name(preset), nullptr, shell.active == preset)) {
                rv_editor_shell_switch(shell, preset);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(rv_editor_text("shell_menu.reset_layout"))) {
        rv_editor_shell_reset_layout(shell, shell.active);
    }
}

void rv_editor_menu_help(rv_editor_shell &shell)
{
    if (ImGui::MenuItem(rv_editor_text("manual.menu_item"), "F1")) {
        rv_editor_shell_page(shell, rv_editor_start_page::manual);
    }
    if (ImGui::MenuItem(rv_editor_text("shell_menu.keyboard_shortcuts"))) {
        rv_editor_shell_page(shell, rv_editor_start_page::help);
    }
}

bool rv_editor_scene_pane_focused(const rv_editor_workspace &ws)
{
    if (ws.focused_leaf >= ws.layout.nodes.size()) {
        return false;
    }
    const rv_editor_tile_node &node = ws.layout.nodes[ws.focused_leaf];
    if (node.kind != rv_editor_tile_kind::leaf || node.leaf.active >= node.leaf.tabs.size()) {
        return false;
    }
    return ws.panes.panes[node.leaf.tabs[node.leaf.active]].kind == rv_editor_pane_kind::scene;
}
} // namespace

void rv_editor_shell_menu(rv_editor_shell &shell)
{
    rv_editor_menu_style_push();
    if (!ImGui::BeginMainMenuBar()) {
        rv_editor_menu_style_pop();
        return;
    }
    struct {
        const char *text_key;
        void (*draw)(rv_editor_shell &);
    } menus[] = { { "catalog_status.menu_file", rv_editor_menu_file },
        { "shell_menu.edit", rv_editor_menu_edit },
        { "shell_menu.view", rv_editor_menu_view },
        { "shell_menu.project", rv_editor_menu_project },
        { "catalog_status.menu_scene", rv_editor_menu_scene },
        { "catalog_status.menu_run", rv_editor_menu_run },
        { "shell_menu.window", rv_editor_menu_window },
        { "shell_menu.help", rv_editor_menu_help } };
    for (const auto &m : menus) {
        if (rv_editor_menu_begin(rv_editor_text(m.text_key))) {
            m.draw(shell);
            ImGui::EndMenu();
        }
    }
    // The layout switch at the bar's right end, as in the references: one place, once a project is open.
    float names = 0.0f;
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        names += ImGui::CalcTextSize(rv_editor_layout_preset_name(preset)).x + 2.0f * ImGui::GetStyle().ItemSpacing.x;
    }
    const float at = ImGui::GetWindowWidth() - names - ImGui::GetStyle().WindowPadding.x;
    if (shell.app.project.open && at > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(at);
        // Own ID scope: "Scene" is also a menu on this bar.
        ImGui::PushID("workspaces");
        for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
            if (ImGui::MenuItem(rv_editor_layout_preset_name(preset), nullptr, shell.active == preset)) {
                rv_editor_shell_switch(shell, preset);
            }
        }
        ImGui::PopID();
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
    // Ctrl+S in a Code tile stays with nvim; only a focused Scene pane saves the scene.
    if (!ImGui::GetIO().WantTextInput && rv_editor_scene_pane_focused(shell.ws) &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        rv_editor_shell_scene_save(shell);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F)) {
        app.project_search.focus = true;
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::search);
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F1)) {
        rv_editor_shell_page(shell, rv_editor_start_page::manual);
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F5)) {
        rv_editor_app_run(app);
    }
    // Tab belongs to the code editor and the terminal; these move between tiles.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F6)) {
        rv_editor_shell_focus_next(shell, false);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F6)) {
        rv_editor_shell_focus_next(shell, true);
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
    if (ImGui::IsKeyChordPressed(ImGuiKey_F8)) {
        rv_editor_app_reload(app);
    }
}

bool rv_editor_shell_scale_fits(SDL_Window *window, float scale)
{
    if (!window) {
        return false;
    }
    SDL_DisplayID display_id = SDL_GetDisplayForWindow(window);
    if (display_id == 0) {
        return false;
    }
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display_id);
    if (!mode) {
        return false;
    }
    // Check if 1280*scale x 720*scale pixels fits on display.
    // Display mode size is in points; convert to pixels using pixel density.
    int display_w_pixels = static_cast<int>(mode->w * mode->pixel_density);
    int display_h_pixels = static_cast<int>(mode->h * mode->pixel_density);
    int needed_w = static_cast<int>(std::ceil(window_min_width * scale));
    int needed_h = static_cast<int>(std::ceil(window_min_height * scale));
    return needed_w <= display_w_pixels && needed_h <= display_h_pixels;
}

void rv_editor_shell_new_file(rv_editor_shell &shell)
{
    rv_editor_menu_code_keys(shell, "<Cmd>enew<CR>");
}

void rv_editor_shell_ask_terminal(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_ask_begin(rv_editor_text("shell_menu.ask_terminal_title"), theme);
    ImGui::TextWrapped("%s", rv_editor_text("shell_menu.ask_terminal_message"));
    if (rv_editor_button(rv_editor_text("shell_menu.ask_terminal_end"), theme)) {
        (void)rv_editor_tile_remove(shell.ws.layout, shell.closing_terminal);
        shell.closing_terminal = rv_editor_tile_none;
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("shell_menu.ask_terminal_keep"), theme)) {
        shell.closing_terminal = rv_editor_tile_none;
    }
    rv_editor_ask_end();
}

} // namespace rv_editor
