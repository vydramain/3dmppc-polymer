// The window's menu bar: File, Edit, View, Project, Run, Window,
// Help and the layout switch at its right end.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "prefs/rv_editor_prefs.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

constexpr float menu_centering_divisor = 2.0f;
constexpr float item_spacings_per_name = 2.0f;

// nvim key sequences sent from Edit/File menus to the focused code tile
constexpr const char *code_keys_new_buffer = "<Cmd>enew<CR>";
constexpr const char *code_keys_save = "<C-s>";
constexpr const char *code_keys_undo = "<C-z>";
constexpr const char *code_keys_redo = "<C-S-z>";

// A top-level menu with its mnemonic, the first letter, underlined. Alt
// moves the keyboard into the bar; the arrows walk it.
bool rv_editor_top_menu_begin(const char *label)
{
    const bool open = rv_editor_menu_begin(label, nullptr);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float text = ImGui::CalcTextSize(label).x;
    const float x = std::floor(min.x + (max.x - min.x - text) / menu_centering_divisor);
    const char first[] = { label[0], '\0' };
    // Below the baseline; the bar's own clip would cut it, so it goes on top.
    const float y = std::floor(min.y + (max.y - min.y + ImGui::GetFontSize()) / menu_centering_divisor) - 1.0f;
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(x, y),
        ImVec2(x + ImGui::CalcTextSize(first).x, y + 1.0f),
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
        rv_editor_menu_code_keys(shell, code_keys_new_buffer);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_start.new_project"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::new_project),
            nullptr)) {
        rv_editor_shell_page(shell, rv_editor_start_page::new_project);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_start.open_project"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::open_project),
            nullptr)) {
        rv_editor_shell_open_project(shell);
    }
    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.save"), rv_editor_text("shell_menu.shortcut_save"), no_code)) {
        rv_editor_menu_code_keys(shell, code_keys_save);
    }
    if (rv_editor_menu_item(rv_editor_text("catalog_status.menu_save_all"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::save_all),
            nullptr)) {
        rv_editor_shell_save_all(shell);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.save_as"), nullptr, nullptr)) {
        rv_editor_shell_save_as_start(shell);
    }
    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.settings"), nullptr, nullptr)) {
        rv_editor_shell_page(shell, rv_editor_start_page::settings);
    }
    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.quit"), nullptr, nullptr)) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
}

void rv_editor_menu_edit(rv_editor_shell &shell)
{
    const char *no_code = rv_editor_menu_code_window(shell) == 0 ? rv_editor_text("shell_menu.why_no_code_edit") : nullptr;
    if (rv_editor_menu_item(rv_editor_text("shell_menu.undo"), rv_editor_text("shell_menu.shortcut_undo"), no_code)) {
        rv_editor_menu_code_keys(shell, code_keys_undo);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.redo"), rv_editor_text("shell_menu.shortcut_redo"), no_code)) {
        rv_editor_menu_code_keys(shell, code_keys_redo);
    }
    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.find_in_project"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::find),
            nullptr)) {
        shell.app.project_search.focus = true;
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::search);
    }
}

void rv_editor_menu_view(rv_editor_shell &shell)
{
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.code_text_size"), nullptr)) {
        struct {
            rv_editor_code_size size;
            const char *text_key;
        } sizes[] = { { rv_editor_code_size::small, "shell_menu.size_small" },
            { rv_editor_code_size::normal, "shell_menu.size_normal" },
            { rv_editor_code_size::large, "shell_menu.size_large" } };
        for (const auto &s : sizes) {
            if (rv_editor_menu_choice(rv_editor_text(s.text_key), nullptr, rv_editor_font_code_size() == s.size, nullptr)) {
                rv_editor_font_code_size_set(s.size);
            }
        }
        rv_editor_menu_end();
    }
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.game_scale"), nullptr)) {
        struct {
            rv_editor_game_scale scale;
            const char *text_key;
        } scales[] = { { rv_editor_game_scale::fit, "pane_game.scale_fit" },
            { rv_editor_game_scale::integer, "pane_game.scale_integer" },
            { rv_editor_game_scale::x1, "pane_game.scale_1x" },
            { rv_editor_game_scale::x2, "pane_game.scale_2x" },
            { rv_editor_game_scale::x3, "pane_game.scale_3x" } };
        for (const auto &s : scales) {
            const bool on = shell.ui.game_scale == s.scale;
            if (rv_editor_menu_choice(rv_editor_text(s.text_key), nullptr, on, nullptr)) {
                shell.ui.game_scale = s.scale;
            }
        }
        rv_editor_menu_end();
    }
    // Independent of the code text size and of the Game scale.
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.ui_scale"), nullptr)) {
        struct {
            float scale;
            const char *text_key;
        } scales[] = { { 1.0f, "shell_menu.ui_scale_1x" },
            { ui_scale_medium, "shell_menu.ui_scale_15x" },
            { ui_scale_large, "shell_menu.ui_scale_2x" } };
        for (const auto &s : scales) {
            const bool fits = rv_editor_shell_scale_fits(shell.window, s.scale);
            const char *why_not = nullptr;
            std::string why_not_str;
            if (!fits && s.scale != 1.0f) {
                const int needed_w = static_cast<int>(std::ceil(static_cast<float>(window_min_width) * s.scale));
                const int needed_h = static_cast<int>(std::ceil(static_cast<float>(window_min_height) * s.scale));
                why_not_str =
                    rv_editor_text_format("shell_menu.scale_needs_display", std::make_format_args(needed_w, needed_h));
                why_not = why_not_str.c_str();
            }
            if (rv_editor_menu_choice(rv_editor_text(s.text_key), nullptr, shell.ui_scale == s.scale, why_not)) {
                shell.ui_scale_request = s.scale;
            }
        }
        rv_editor_menu_end();
    }
    rv_editor_separator();
    // Independent of the focused tile and of a project.
    if (rv_editor_menu_item(rv_editor_text("shell_menu.widget_catalog"), nullptr, nullptr)) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::catalog);
    }
}

void rv_editor_menu_project(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    if (rv_editor_menu_item(rv_editor_text("shell_menu.project_settings"), nullptr, nullptr)) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::project);
    }
    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("widgets_status.button_build"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::build),
            rv_editor_app_why_not_build(app))) {
        rv_editor_app_build(app);
    }
    const bool cancel_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cancel_build);
    const bool can_cancel = app.build.state() == rv_editor_build_state::building;
    const bool cancelling = app.build.state() == rv_editor_build_state::cancelling;
    const char *why_not_cancel = cancel_pending ? rv_editor_text("app_why_not.cancel_pending") :
        can_cancel                              ? nullptr :
        cancelling                              ? rv_editor_text("panes.already_cancelling") :
                                                  rv_editor_text("shell_menu.why_no_build");
    if (rv_editor_menu_item(rv_editor_text("shell_menu.cancel_build"), nullptr, why_not_cancel)) {
        rv_editor_app_build_cancel(app);
    }
}

void rv_editor_menu_run(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    const bool paused = app.session.state() == rv_editor_run_state::paused;
    const char *resume_text = rv_editor_text("widgets_status.button_resume");
    const char *run_text = rv_editor_text("widgets_status.button_run");
    const char *run_label = paused ? resume_text : run_text;
    const char *shortcut = rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::run);
    if (rv_editor_menu_item(run_label, shortcut, rv_editor_app_why_not_run(app))) {
        rv_editor_app_run(app);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.run_last"), nullptr, rv_editor_app_why_not_run_last(app))) {
        rv_editor_app_run_last(app);
    }
    if (rv_editor_menu_item(rv_editor_text("widgets_status.button_pause"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::pause),
            rv_editor_app_why_not_pause(app))) {
        rv_editor_app_pause(app);
    }
    if (rv_editor_menu_item(rv_editor_text("widgets_status.button_step_frame"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::step),
            rv_editor_app_why_not_step(app))) {
        rv_editor_app_step(app);
    }
    if (rv_editor_menu_item(rv_editor_text("widgets_status.button_stop"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::stop),
            rv_editor_app_why_not_stop(app))) {
        rv_editor_app_stop(app);
    }
    const bool can_reload = app.session.live() && rv_editor_app_can_reload(app);
    const char *why_not_reload = can_reload ? rv_editor_app_why_not_reload(app) : rv_editor_text("shell_menu.why_no_reload");
    const rv_editor_change_plan reload_plan =
        can_reload ? rv_editor_app_change_for(app, app.code_file) : rv_editor_change_plan{};
    std::string reload_label;
    switch (reload_plan.action) {
    case rv_editor_change_action::reload_module:
        reload_label = rv_editor_text_format("shell_menu.reload_module", std::make_format_args(reload_plan.name));
        break;
    case rv_editor_change_action::refresh_texture:
        reload_label = rv_editor_text_format("shell_menu.refresh_texture", std::make_format_args(reload_plan.name));
        break;
    default:
        reload_label = rv_editor_text("widgets_status.tooltip_reload");
    }
    if (rv_editor_menu_item(reload_label.c_str(),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::reload),
            why_not_reload)) {
        rv_editor_app_reload(app);
    }
    const char *why_not_build_restart =
        app.session.live() ? rv_editor_app_why_not_build(app) : rv_editor_text("shell_menu.why_no_game");
    if (rv_editor_menu_item(rv_editor_text("widgets_status.tooltip_restart"), nullptr, why_not_build_restart)) {
        rv_editor_app_build_restart(app);
    }
    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.force_stop"),
            nullptr,
            app.session.live() ? nullptr : rv_editor_text("shell_menu.why_no_runtime"))) {
        rv_editor_app_force_stop(app);
    }
    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.run_config"),
            nullptr,
            app.project.open ? nullptr : rv_editor_text("shell_menu.why_no_project"))) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::run_config, true);
    }
}

void rv_editor_menu_window(rv_editor_shell &shell)
{
    if (rv_editor_menu_item(rv_editor_text("shell_menu.terminal"), nullptr, nullptr)) {
        rv_editor_shell_focus_terminal(shell);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.focus_next_pane"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::focus_next),
            nullptr)) {
        rv_editor_shell_focus_next(shell, false);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.focus_prev_pane"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::focus_previous),
            nullptr)) {
        rv_editor_shell_focus_next(shell, true);
    }
    rv_editor_separator();
    // Every kind of tile, beside the focused one.
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.new_tile"), nullptr)) {
        for (uint32_t k = 1; k <= static_cast<uint32_t>(rv_editor_pane_kind_last); ++k) {
            const auto kind = static_cast<rv_editor_pane_kind>(k);
            if (rv_editor_menu_item(rv_editor_pane_title(kind), nullptr, nullptr)) {
                rv_editor_shell_new_tile(shell, kind);
            }
        }
        rv_editor_menu_end();
    }
    rv_editor_separator();
    // The same four the bar's right end switches between; each keeps its tiles.
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.reference_layouts"), nullptr)) {
        for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
            if (rv_editor_menu_choice(rv_editor_layout_preset_name(preset), nullptr, shell.active == preset, nullptr)) {
                rv_editor_shell_switch(shell, preset);
            }
        }
        rv_editor_menu_end();
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.reset_layout"), nullptr, nullptr)) {
        rv_editor_shell_reset_layout(shell, shell.active);
    }
}

void rv_editor_menu_help(rv_editor_shell &shell)
{
    if (rv_editor_menu_item(rv_editor_text("manual.menu_item"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::manual),
            nullptr)) {
        rv_editor_shell_page(shell, rv_editor_start_page::manual);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.keyboard_shortcuts"), nullptr, nullptr)) {
        rv_editor_shell_page(shell, rv_editor_start_page::help);
    }
}

} // namespace

void rv_editor_shell_menu(rv_editor_shell &shell)
{
    if (!rv_editor_main_menu_bar_begin()) {
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
        if (rv_editor_top_menu_begin(rv_editor_text(m.text_key))) {
            m.draw(shell);
            rv_editor_menu_end();
        }
    }
    // The layout switch at the bar's right end, as in the references: one place, once a project is open.
    float names = 0.0f;
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        names += ImGui::CalcTextSize(rv_editor_layout_preset_name(preset)).x +
            item_spacings_per_name * ImGui::GetStyle().ItemSpacing.x;
    }
    const float at = ImGui::GetWindowWidth() - names - ImGui::GetStyle().WindowPadding.x;
    if (shell.app.project.open && at > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(at);
        // Own ID scope: "Scene" is also a menu on this bar.
        ImGui::PushID("workspaces");
        for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
            if (rv_editor_menu_choice(rv_editor_layout_preset_name(preset), nullptr, shell.active == preset, nullptr)) {
                rv_editor_shell_switch(shell, preset);
            }
        }
        ImGui::PopID();
    }
    rv_editor_main_menu_bar_end();
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
    rv_editor_menu_code_keys(shell, code_keys_new_buffer);
}

void rv_editor_shell_terminal_answered(rv_editor_shell &shell, rv_editor_pane_id pane)
{
    rv_editor_terminal_view &view = shell.app.terminals[pane];
    if (!view.ending || view.end_answer == rv_editor_terminal_end_answer::none) {
        return;
    }
    if (view.end_answer == rv_editor_terminal_end_answer::end) {
        // The layout is being drawn: the close waits for the workspace's own apply, which asks the shell again and
        // finds the answer.
        shell.ws.pending.what = rv_editor_tile_action::op::close;
        shell.ws.pending.pane = pane;
        return;
    }
    view.end_answer = rv_editor_terminal_end_answer::none;
    shell.closing_terminal = rv_editor_tile_none;
}

} // namespace rv_editor
