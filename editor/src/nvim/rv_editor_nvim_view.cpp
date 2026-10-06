// A code tile: one nvim window's grid drawn with the code font, and the
// keyboard turned into nvim_input notation while the tile has focus.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "font/rv_editor_font.hpp"

#include "panes/rv_editor_panes.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// nvim mouse operation names.
constexpr const char *nvim_mouse_button_left = "left";
constexpr const char *nvim_mouse_button_wheel = "wheel";
constexpr const char *nvim_mouse_action_press = "press";
constexpr const char *nvim_mouse_action_drag = "drag";
constexpr const char *nvim_mouse_action_release = "release";
constexpr const char *nvim_mouse_dir_up = "up";
constexpr const char *nvim_mouse_dir_down = "down";
constexpr const char *nvim_mouse_dir_left = "left";
constexpr const char *nvim_mouse_dir_right = "right";

// nvim protocol state values: swap file states and LSP server states.
constexpr const char *nvim_swap_state_recoverable = "recoverable";
constexpr const char *nvim_swap_state_in_use = "in_use";
constexpr const char *nvim_lsp_state_running = "running";
constexpr const char *nvim_lsp_state_missing = "missing";

// Code grid dimensions and layout.
constexpr int tile_min_rows = 3;                   // Minimum tile height in rows: two UI lines + one grid row.
constexpr int grid_ui_reserved_rows = 2;           // Rows reserved for status and command line.
constexpr const char *grid_cell_width_probe = "M"; // Glyph to measure monospace cell width.
constexpr int status_line_text_offset_cells = 2;   // Horizontal offset for notes on status line.
constexpr size_t min_tabs_for_close = 2;           // Minimum tabs required to allow closing one.

} // namespace

namespace
{

void rv_editor_pane_code_body(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// The files this code tile has shown and nvim still holds, one tab each, the one
// in front in brass. A click shows that file in the tile; a file opened again
// only brings its tab forward, as :edit switches to the buffer it already has.
void rv_editor_code_tab_row(rv_editor_app &app, rv_editor_pane_id pane, int64_t win, const rv_editor_theme &theme)
{
    rv_editor_nvim &nvim = app.nvim;
    const auto held = [&nvim](const std::string &name) -> const rv_editor_nvim_buffer * {
        for (const rv_editor_nvim_buffer &b : nvim.buffers()) {
            if (!name.empty() && b.name == name) {
                return &b;
            }
        }
        return nullptr;
    };
    rv_editor_code_tabs &tabs = app.code_tabs[pane];
    std::erase_if(tabs.names, [&held](const std::string &name) { return held(name) == nullptr; });
    const rv_editor_nvim_buffer *front = nvim.buffer_in(win);
    const std::string shown = front != nullptr ? front->name : std::string();
    if (shown != tabs.dropped) {
        tabs.dropped.clear();
    }
    if (!shown.empty() && tabs.dropped.empty() &&
        std::find(tabs.names.begin(), tabs.names.end(), shown) == tabs.names.end()) {
        tabs.names.push_back(shown);
    }

    std::vector<std::string> text;
    int active = -1;
    for (const std::string &name : tabs.names) {
        if (name == shown) {
            active = static_cast<int>(text.size());
        }
        const std::string marker = held(name)->modified ? rv_editor_text("pane_code.file_modified_marker") : "";
        text.push_back(std::filesystem::path(name).filename().string() + marker + "##" + name);
    }
    if (front != nullptr && shown.empty()) {
        active = static_cast<int>(text.size());
        text.push_back(std::string(rv_editor_text("pane_code.untitled_document")) +
            (front->modified ? rv_editor_text("pane_code.file_modified_marker") : ""));
    }
    if (text.empty()) {
        return;
    }
    std::vector<const char *> labels;
    for (const std::string &t : text) {
        labels.push_back(t.c_str());
    }
    ImGui::SameLine();
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    int picked = active;
    if (rv_editor_tab_strip("##files", labels.data(), static_cast<int>(labels.size()), &picked, theme) &&
        picked < static_cast<int>(tabs.names.size())) {
        nvim.open(win, tabs.names[static_cast<size_t>(picked)], 0);
    }

    // A right click on the tabs: Close Tab lets go of the file in front, which
    // nvim keeps loaded; an unsaved one stays until it is saved.
    if (ImGui::IsMouseHoveringRect(row_min, ImGui::GetItemRectMax()) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##tabmenu");
    }
    rv_editor_menu_style_push();
    if (ImGui::BeginPopup("##tabmenu")) {
        const bool named = active >= 0 && active < static_cast<int>(tabs.names.size());
        const char *why_not = nullptr;
        if (!named) {
            why_not = rv_editor_text("pane_code.close_tab_untitled");
        } else if (tabs.names.size() < min_tabs_for_close) {
            why_not = rv_editor_text("pane_code.close_tab_only_file");
        } else if (held(tabs.names[static_cast<size_t>(active)])->modified) {
            why_not = rv_editor_text("pane_code.close_tab_unsaved");
        }
        if (ImGui::MenuItem(rv_editor_text("pane_code.close_tab_menu_item"), nullptr, false, why_not == nullptr)) {
            const size_t at = static_cast<size_t>(active);
            tabs.dropped = tabs.names[at];
            tabs.names.erase(tabs.names.begin() + static_cast<std::ptrdiff_t>(at));
            nvim.open(win, tabs.names[std::min(at, tabs.names.size() - 1)], 0);
        }
        if (why_not != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", why_not);
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();
}

} // namespace

// Code is drawn in the code font (0004), the tile's buttons in the interface font.
void rv_editor_pane_code(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    rv_editor_font_code_push();
    rv_editor_pane_code_body(app, pane, theme);
    rv_editor_font_code_pop();
}

namespace
{

void rv_editor_pane_code_body(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    rv_editor_nvim &nvim = app.nvim;
    if (!nvim.running()) {
        (void)nvim.ensure_started(app.project.open ? app.project.root : std::filesystem::current_path(), app.log);
    }
    if (!nvim.problem.empty()) {
        ImGui::TextWrapped("%s", nvim.problem.c_str());
        if (rv_editor_button(rv_editor_text("pane_code.start_nvim_again"), theme)) {
            nvim.problem.clear();
        }
        return;
    }
    const int64_t win = nvim.window_for(pane);
    const int32_t grid_id = nvim.screen().grid_of_window(win);
    const rv_editor_nvim_grid *grid = nvim.screen().grid(grid_id);

    // The tile's own row, in the interface font: Vim or the ordinary editor (the
    // switch F2 also makes), then the tile's files as tabs.
    rv_editor_font_code_pop();
    rv_editor_shelf_begin("##shelf", theme);
    bool vim = nvim.vim_mode();
    const std::string vim_label = std::string(rv_editor_text("pane_code.vim_toggle")) + "##mode";
    if (rv_editor_toggle(vim_label.c_str(), &vim, theme)) {
        nvim.toggle_vim_mode();
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_code.vim_mode_tooltip"));
    rv_editor_code_tab_row(app, pane, win, theme);
    rv_editor_shelf_end();
    // Swap recovery UI when nvim opened a file read-only due to a crashed nvim.
    if (const rv_editor_nvim_buffer *buf = nvim.buffer_in(win); buf != nullptr && !buf->name.empty()) {
        if (const rv_editor_nvim_swap *swap = nvim.swap_for(buf->name)) {
            rv_editor_shelf_begin("##swap", theme);
            if (swap->state == nvim_swap_state_recoverable) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(rv_editor_text("pane_code.swap_recovery_message"));
                ImGui::SameLine();
                std::string name = buf->name;
                if (rv_editor_button(rv_editor_text("pane_code.swap_recover_button"), theme)) {
                    nvim.swap_resolve(win, name, true);
                }
                ImGui::SetItemTooltip("%s", rv_editor_text("pane_code.swap_recover_button_tooltip"));
                ImGui::SameLine();
                if (rv_editor_button(rv_editor_text("pane_code.swap_discard_button"), theme)) {
                    nvim.swap_resolve(win, name, false);
                }
                ImGui::SetItemTooltip("%s", rv_editor_text("pane_code.swap_discard_button_tooltip"));
            } else if (swap->state == nvim_swap_state_in_use) {
                ImGui::AlignTextToFramePadding();
                const long long pid = static_cast<long long>(swap->pid);
                const std::string readonly_msg = rv_editor_text_format("pane_code.readonly_process_editing",
                    std::make_format_args(pid));
                ImGui::Text("%s", readonly_msg.c_str());
            }
            rv_editor_shelf_end();
        }
    }
    rv_editor_font_code_push();
    rv_editor_well_begin("##well", ImVec2(0, 0), theme, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // The code area fills the tile in whole cells, two rows kept under it: the
    // tile's status line and the command line.
    const ImVec2 cell(ImGui::CalcTextSize(grid_cell_width_probe).x, ImGui::GetTextLineHeight());
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int32_t cols = std::max(1, static_cast<int32_t>(avail.x / cell.x));
    const int32_t rows = std::max(tile_min_rows, static_cast<int32_t>(avail.y / cell.y)) - grid_ui_reserved_rows;
    nvim.resize(win, cols, rows);

    ImGui::InvisibleButton("##code", ImVec2(cols * cell.x, (rows + grid_ui_reserved_rows) * cell.y));
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    if (ImGui::IsItemClicked()) {
        nvim.focus(win);
    }
    // The left button goes to nvim as a mouse event in this window's grid, at the
    // cell under the pointer: a click moves the cursor, a drag selects.
    if (grid != nullptr) {
        static int32_t last_row = -1;
        static int32_t last_col = -1;
        const ImVec2 m = ImGui::GetMousePos();
        const int32_t row = std::clamp(static_cast<int32_t>((m.y - at.y) / cell.y), 0, rows - 1);
        const int32_t col = std::clamp(static_cast<int32_t>((m.x - at.x) / cell.x), 0, cols - 1);
        if (ImGui::IsItemActivated()) {
            nvim.mouse(nvim_mouse_button_left, nvim_mouse_action_press, grid_id, row, col);
        } else if (ImGui::IsItemActive() && (row != last_row || col != last_col)) {
            nvim.mouse(nvim_mouse_button_left, nvim_mouse_action_drag, grid_id, row, col);
        } else if (ImGui::IsItemDeactivated()) {
            nvim.mouse(nvim_mouse_button_left, nvim_mouse_action_release, grid_id, row, col);
        }
        last_row = row;
        last_col = col;
        // The wheel scrolls the window under the pointer, one nvim wheel event a
        // notch; Shift or a sideways wheel scrolls it left and right.
        const ImGuiIO &io = ImGui::GetIO();
        if (ImGui::IsItemHovered()) {
            const bool sideways = io.MouseWheelH != 0.0f || io.KeyShift;
            const float amount = io.MouseWheelH != 0.0f ? io.MouseWheelH : io.MouseWheel;
            const int notches = static_cast<int>(std::lround(std::fabs(amount)));
            const char *h_dir = amount > 0.0f ? nvim_mouse_dir_left : nvim_mouse_dir_right;
            const char *v_dir = amount > 0.0f ? nvim_mouse_dir_up : nvim_mouse_dir_down;
            const char *dir = sideways ? h_dir : v_dir;
            for (int i = 0; amount != 0.0f && i < std::max(1, notches); ++i) {
                nvim.mouse(nvim_mouse_button_wheel, dir, grid_id, row, col);
            }
        }
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 tile_end(at.x + cols * cell.x, at.y + (rows + grid_ui_reserved_rows) * cell.y);
    dl->AddRectFilled(at, tile_end, rv_editor_col(rv_editor_mocha_base));
    if (grid == nullptr) {
        dl->AddText(at, rv_editor_col(rv_editor_mocha_subtext0), rv_editor_text("pane_code.starting_nvim"));
        rv_editor_well_end();
        return;
    }
    const bool cursor_here = focused && nvim.screen().cursor_grid() == grid_id;
    rv_editor_nvim_draw_grid(nvim.screen(), *grid, at, cell, rows, cursor_here);

    // Floating windows (hover, diagnostics) above the main grid.
    rv_editor_nvim_draw_floats(nvim.screen(), grid_id, at, cell, cols, rows, focused);

    // The tile's status line: the file, and whether it is saved.
    const ImVec2 status(at.x, at.y + rows * cell.y);
    dl->AddRectFilled(status, ImVec2(at.x + cols * cell.x, status.y + cell.y), rv_editor_col(rv_editor_mocha_surface1));
    std::string label = rv_editor_text("pane_code.untitled_document");
    if (const rv_editor_nvim_buffer *buf = nvim.buffer_in(win)) {
        std::error_code ec;
        const std::filesystem::path rel = app.project.open && !buf->name.empty()
            ? std::filesystem::relative(buf->name, app.project.root, ec)
            : std::filesystem::path(buf->name);
        if (buf->name.empty()) {
            label = rv_editor_text("pane_code.untitled_document");
        } else if (ec || rel.empty()) {
            label = buf->name;
        } else {
            label = rel.string();
        }
        label += buf->modified ? rv_editor_text("pane_code.file_modified_marker") : "";
    }
    dl->AddText(ImVec2(status.x + cell.x, status.y), rv_editor_col(rv_editor_mocha_text), label.c_str());

    // LSP note: nothing when running, otherwise why diagnostics are absent
    // for this file type, with the exact reason as a tooltip.
    float note_x = status.x + cell.x * status_line_text_offset_cells + ImGui::CalcTextSize(label.c_str()).x;
    if (const rv_editor_nvim_buffer *buf = nvim.buffer_in(win); buf != nullptr && !buf->name.empty()) {
        const std::string server = nvim.lsp_server_for(buf->name);
        const rv_editor_nvim_lsp *lsp = server.empty() ? nullptr : nvim.lsp_status(server);
        if (lsp != nullptr && lsp->state != nvim_lsp_state_running) {
            std::string lsp_state;
            if (lsp->state == nvim_lsp_state_missing) {
                lsp_state = rv_editor_text("pane_code.lsp_state_missing");
            } else {
                lsp_state = rv_editor_text("pane_code.lsp_state_other");
            }
            const std::string note =
                rv_editor_text_format("pane_code.lsp_status_format", std::make_format_args(server, lsp_state));
            const ImVec2 note_pos(note_x, status.y);
            dl->AddText(note_pos, rv_editor_rgb(theme.code_yellow), note.c_str());
            if (!lsp->reason.empty()) {
                ImGui::SetCursorScreenPos(note_pos);
                ImGui::PushID(static_cast<int>(win));
                ImGui::InvisibleButton("##lsp_note", ImGui::CalcTextSize(note.c_str()));
                ImGui::PopID();
                ImGui::SetItemTooltip("%s", lsp->reason.c_str());
            }
            note_x += ImGui::CalcTextSize(note.c_str()).x + cell.x;
        }
    }

    // What saving this file does to the running game, from the change classifier.
    if (const rv_editor_nvim_buffer *buf = nvim.buffer_in(win);
        app.session.live() && buf != nullptr && !buf->name.empty()) {
        const rv_editor_change_plan plan = rv_editor_app_change_for(app, buf->name);
        const char *text = nullptr;
        std::string owned;
        bool warn = false;
        switch (plan.action) {
        case rv_editor_change_action::reload_entry:
            text = rv_editor_text("pane_code.reload_entry_script");
            break;
        case rv_editor_change_action::reload_module:
            owned = rv_editor_text_format("pane_code.reload_module_format", std::make_format_args(plan.name));
            text = owned.c_str();
            break;
        case rv_editor_change_action::refresh_texture:
            owned = rv_editor_text_format("pane_code.refresh_texture_format", std::make_format_args(plan.name));
            text = owned.c_str();
            break;
        case rv_editor_change_action::build_restart:
        case rv_editor_change_action::restart_required:
            text = rv_editor_text("pane_code.build_restart");
            warn = true;
            break;
        case rv_editor_change_action::not_in_disc:
            text = rv_editor_text("pane_code.not_on_running_disc");
            warn = true;
            break;
        case rv_editor_change_action::none:
            break;
        }
        if (text != nullptr) {
            const ImVec2 note_pos(note_x, status.y);
            dl->AddText(note_pos, rv_editor_rgb(warn ? theme.code_yellow : theme.code_blue), text);
            if (!plan.reason.empty()) {
                ImGui::SetCursorScreenPos(note_pos);
                ImGui::PushID(static_cast<int>(win));
                ImGui::InvisibleButton("##save_note", ImGui::CalcTextSize(text));
                ImGui::PopID();
                ImGui::SetItemTooltip("%s", plan.reason.c_str());
            }
        }
    }

    if (!focused) {
        rv_editor_well_end();
        return;
    }
    // The command line and messages under the tile being typed in. The message
    // grid is as tall as grid 1, but only its rows from msg_row down are shown.
    const ImVec2 bottom(at.x, at.y + (rows + 1) * cell.y);
    const rv_editor_nvim_grid *global = nvim.screen().grid(1);
    const rv_editor_nvim_grid *msg = nvim.screen().grid(nvim.screen().message_grid());
    const int32_t msg_rows = global != nullptr ? global->height - nvim.screen().message_row() : 0;
    if (msg != nullptr && !msg->hidden && msg_rows > 0) {
        const int32_t shown = std::min(msg_rows, rows + 1);
        rv_editor_nvim_draw_grid(nvim.screen(), *msg, ImVec2(at.x, bottom.y - (shown - 1) * cell.y), cell, shown,
            nvim.screen().cursor_grid() == nvim.screen().message_grid());
    } else if (global != nullptr && global->height > 0) {
        rv_editor_nvim_grid line;
        line.width = global->width;
        line.height = 1;
        line.cells.assign(global->cells.end() - global->width, global->cells.end());
        line.cursor_row = nvim.screen().cursor_grid() == 1 ? 0 : -1;
        line.cursor_col = global->cursor_col;
        rv_editor_nvim_draw_grid(nvim.screen(), line, bottom, cell, 1, line.cursor_row == 0);
    }

    // Keys go to nvim; ImGui's own keyboard navigation stays off meanwhile.
    app.text_focus = true;
    nvim.focus(win);
    if (SDL_Window *w = SDL_GetKeyboardFocus(); w != nullptr && !SDL_TextInputActive(w)) {
        SDL_StartTextInput(w);
    }
    nvim.input(rv_editor_nvim_keys());
    rv_editor_well_end();
}

} // namespace

} // namespace rv_editor
