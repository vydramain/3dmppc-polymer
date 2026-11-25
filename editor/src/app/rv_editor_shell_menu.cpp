// The main menu bar and workspace shortcuts.

#include "app/rv_editor_shell.hpp"

#include <cmath>
#include <string>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "prefs/rv_editor_prefs.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

constexpr float menu_centering_divisor = 2.0f;
constexpr float item_spacings_per_name = 2.0f;

constexpr const char *code_keys_new_buffer = "<Cmd>enew<CR>";

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

// The code tile used last, and its nvim window, for New File.
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

void rv_editor_menu_project(rv_editor_shell &shell)
{
    using action = rv_editor_shortcut_action;
    const char *blocked = rv_editor_shell_why_not_project_switch(shell);
    const char *no_project = shell.app.project.open ? nullptr : rv_editor_text("shell_menu.why_no_project");
    if (rv_editor_menu_item(rv_editor_text("shell_menu.new_project"),
            rv_editor_shell_shortcut_label(shell, action::new_project),
            blocked)) {
        rv_editor_shell_page(shell, rv_editor_start_page::new_project);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.open_project"),
            rv_editor_shell_shortcut_label(shell, action::open_project),
            blocked)) {
        rv_editor_shell_open_project(shell);
    }
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.open_recent"), nullptr)) {
        rv_editor_shell_recent_menu(shell);
        rv_editor_menu_end();
    }

    rv_editor_separator();
    rv_editor_menu_document_save(shell);
    rv_editor_separator();
    rv_editor_menu_document_history(shell);
    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.project_settings"), nullptr, no_project)) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::project);
    }

    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.close_project"),
            nullptr,
            no_project != nullptr ? no_project : blocked)) {
        rv_editor_shell_request_close_project(shell);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.quit"), nullptr, nullptr)) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
}

void rv_editor_menu_layout_choices(rv_editor_shell &shell, const char *why_not)
{
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        if (rv_editor_menu_choice(rv_editor_layout_preset_name(preset), nullptr, shell.active == preset, why_not)) {
            rv_editor_shell_switch(shell, preset);
        }
    }
}

void rv_editor_menu_ui_scale(rv_editor_shell &shell)
{
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.ui_scale"), nullptr)) {
        constexpr struct {
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
}

void rv_editor_menu_font_sizes(rv_editor_shell &shell)
{
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.main_font_size"), nullptr)) {
        for (const rv_editor_main_size size : { rv_editor_main_size::normal, rv_editor_main_size::large }) {
            const char *label = rv_editor_text(
                size == rv_editor_main_size::normal ? "shell_menu.main_size_normal" : "shell_menu.main_size_large");
            if (rv_editor_menu_choice(label, nullptr, shell.main_size == size, nullptr)) {
                shell.main_size_request = size;
            }
        }
        rv_editor_menu_end();
    }
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.code_font_size"), nullptr)) {
        constexpr struct {
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
}

void rv_editor_menu_editor(rv_editor_shell &shell)
{
    const char *no_project = shell.app.project.open ? nullptr : rv_editor_text("shell_menu.why_no_project");
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.new_tile"), nullptr)) {
        for (uint32_t k = 1; k <= static_cast<uint32_t>(rv_editor_pane_kind_last); ++k) {
            const auto kind = static_cast<rv_editor_pane_kind>(k);
            if (rv_editor_menu_item(rv_editor_pane_title(kind), nullptr, no_project)) {
                rv_editor_shell_new_tile(shell, kind);
            }
        }
        rv_editor_menu_end();
    }

    rv_editor_separator();
    if (rv_editor_menu_begin(rv_editor_text("shell_menu.layout"), nullptr)) {
        rv_editor_menu_layout_choices(shell, no_project);
        rv_editor_menu_end();
    }
    if (rv_editor_menu_item(rv_editor_text("shell_menu.reset_layout"), nullptr, no_project)) {
        rv_editor_shell_reset_layout(shell, shell.active);
    }

    rv_editor_separator();
    rv_editor_menu_ui_scale(shell);
    rv_editor_menu_font_sizes(shell);

    rv_editor_separator();
    if (rv_editor_menu_item(rv_editor_text("shell_menu.settings"), nullptr, nullptr)) {
        rv_editor_shell_page(shell, rv_editor_start_page::settings);
    }
}

bool rv_editor_menu_help(rv_editor_shell &shell)
{
    if (rv_editor_menu_item(rv_editor_text("manual.menu_item"),
            rv_editor_shell_shortcut_label(shell, rv_editor_shortcut_action::manual),
            nullptr)) {
        rv_editor_shell_page(shell, rv_editor_start_page::manual);
    }
    rv_editor_separator();
    return rv_editor_menu_item(rv_editor_text("shell_menu.about"), nullptr, nullptr);
}

void rv_editor_menu_workspace_shortcuts(rv_editor_shell &shell)
{
    if (!shell.app.project.open) {
        return;
    }
    float names = 0.0f;
    for (const rv_editor_layout_preset preset : rv_editor_workspaces) {
        names += ImGui::CalcTextSize(rv_editor_layout_preset_name(preset)).x +
            item_spacings_per_name * ImGui::GetStyle().ItemSpacing.x;
    }
    const float at = ImGui::GetWindowWidth() - names - ImGui::GetStyle().WindowPadding.x;
    if (at > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(at);
        ImGui::PushID("workspaces");
        rv_editor_menu_layout_choices(shell, nullptr);
        ImGui::PopID();
    }
}

} // namespace

void rv_editor_shell_menu(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    if (!rv_editor_main_menu_bar_begin()) {
        return;
    }
    const char *project = rv_editor_text("shell_menu.project");
    const ImGuiID project_id = ImGui::GetID(project);
    if (rv_editor_top_menu_begin(project)) {
        rv_editor_menu_document_open(shell, project_id);
        rv_editor_menu_project(shell);
        rv_editor_menu_end();
    }
    if (!ImGui::IsPopupOpen(project)) {
        shell.menu_document.menu = 0;
    }
    if (rv_editor_top_menu_begin(rv_editor_text("shell_menu.editor"))) {
        rv_editor_menu_editor(shell);
        rv_editor_menu_end();
    }
    bool show_about = false;
    if (rv_editor_top_menu_begin(rv_editor_text("shell_menu.help"))) {
        show_about = rv_editor_menu_help(shell);
        rv_editor_menu_end();
    }

    rv_editor_menu_workspace_shortcuts(shell);
    if (show_about) {
        ImGui::OpenPopup("##about_editor");
    }
    if (rv_editor_popup_begin("##about_editor")) {
        rv_editor_page_about(theme);
        rv_editor_popup_end();
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
