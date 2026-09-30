// A code tile: one nvim window's grid drawn with the code font, and the
// keyboard turned into nvim_input notation while the tile has focus.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#include "font/rv_editor_font.hpp"

#include "panes/rv_editor_panes.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

ImU32 rv_editor_rgb(uint32_t rgb)
{
    return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
}

// One grid, cell by cell, runs of one highlight drawn together.
void rv_editor_nvim_draw_grid(const rv_editor_nvim_screen &screen, const rv_editor_nvim_grid &grid, ImVec2 at,
    ImVec2 cell, int32_t rows, bool cursor)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    std::string run;
    for (int32_t r = 0; r < std::min(rows, grid.height); ++r) {
        int32_t c = 0;
        while (c < grid.width) {
            const int32_t hl = grid.cells[static_cast<size_t>(r * grid.width + c)].hl;
            const int32_t start = c;
            run.clear();
            while (c < grid.width && grid.cells[static_cast<size_t>(r * grid.width + c)].hl == hl) {
                const std::string &t = grid.cells[static_cast<size_t>(r * grid.width + c)].text;
                run += t; // a double-width character's second cell is empty
                ++c;
            }
            uint32_t fg = 0;
            uint32_t bg = 0;
            screen.colors(hl, fg, bg);
            const ImVec2 p0(at.x + start * cell.x, at.y + r * cell.y);
            dl->AddRectFilled(p0, ImVec2(at.x + c * cell.x, p0.y + cell.y), rv_editor_rgb(bg));
            dl->AddText(p0, rv_editor_rgb(fg), run.c_str());
            if (screen.attr(hl).underline) {
                dl->AddLine(ImVec2(p0.x, p0.y + cell.y - 1), ImVec2(at.x + c * cell.x, p0.y + cell.y - 1), rv_editor_rgb(fg));
            }
        }
    }
    if (!cursor || grid.cursor_row < 0 || grid.cursor_row >= std::min(rows, grid.height) || grid.cursor_col < 0 ||
        grid.cursor_col >= grid.width) {
        return;
    }
    // The shape nvim gives the current mode (guicursor, mode_info_set).
    const rv_editor_nvim_cell &under = grid.cells[static_cast<size_t>(grid.cursor_row * grid.width + grid.cursor_col)];
    uint32_t fg = 0;
    uint32_t bg = 0;
    screen.colors(under.hl, fg, bg);
    const ImVec2 p0(at.x + grid.cursor_col * cell.x, at.y + grid.cursor_row * cell.y);
    const rv_editor_nvim_cursor shape = screen.cursor_shape();
    if (shape.kind == rv_editor_nvim_cursor_kind::vertical) {
        const float w = std::max(2.0f, std::floor(cell.x * static_cast<float>(shape.percent) / 100.0f));
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + cell.y), rv_editor_rgb(fg));
        return;
    }
    if (shape.kind == rv_editor_nvim_cursor_kind::horizontal) {
        const float h = std::max(2.0f, std::floor(cell.y * static_cast<float>(shape.percent) / 100.0f));
        dl->AddRectFilled(ImVec2(p0.x, p0.y + cell.y - h), ImVec2(p0.x + cell.x, p0.y + cell.y), rv_editor_rgb(fg));
        return;
    }
    dl->AddRectFilled(p0, ImVec2(p0.x + cell.x, p0.y + cell.y), rv_editor_rgb(fg));
    dl->AddText(p0, rv_editor_rgb(bg), under.text.c_str());
}

// One code point as UTF-8 (ImGui's own encoder is internal API, 0001).
void rv_editor_utf8_append(std::string &out, uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
}

// Key name in nvim_input notation, or nullptr for keys typed as text.
const char *rv_editor_nvim_key(ImGuiKey key)
{
    switch (key) {
        case ImGuiKey_Enter:
        case ImGuiKey_KeypadEnter: return "CR";
        case ImGuiKey_Backspace: return "BS";
        case ImGuiKey_Tab: return "Tab";
        case ImGuiKey_Escape: return "Esc";
        case ImGuiKey_Delete: return "Del";
        case ImGuiKey_Insert: return "Insert";
        case ImGuiKey_Home: return "Home";
        case ImGuiKey_End: return "End";
        case ImGuiKey_PageUp: return "PageUp";
        case ImGuiKey_PageDown: return "PageDown";
        case ImGuiKey_LeftArrow: return "Left";
        case ImGuiKey_RightArrow: return "Right";
        case ImGuiKey_UpArrow: return "Up";
        case ImGuiKey_DownArrow: return "Down";
        case ImGuiKey_F1: return "F1";
        case ImGuiKey_F2: return "F2";
        case ImGuiKey_F3: return "F3";
        case ImGuiKey_F4: return "F4";
        case ImGuiKey_F8: return "F8";
        case ImGuiKey_F9: return "F9";
        case ImGuiKey_F10: return "F10";
        case ImGuiKey_F11: return "F11";
        case ImGuiKey_F12: return "F12";
        default: return nullptr;
    }
}

// This frame's keyboard as nvim keys. F5, F6, F7 and Ctrl+B stay the editor's
// own (section 12); everything else typed into a focused code tile is nvim's.
std::string rv_editor_nvim_keys()
{
    const ImGuiIO &io = ImGui::GetIO();
    std::string keys;
    std::string mods;
    if (io.KeyCtrl) {
        mods += "C-";
    }
    if (io.KeyAlt) {
        mods += "M-";
    }
    if (io.KeySuper) {
        mods += "D-";
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const ImGuiKey key = static_cast<ImGuiKey>(k);
        if (!ImGui::IsKeyPressed(key, true)) {
            continue;
        }
        if (const char *name = rv_editor_nvim_key(key)) {
            keys += "<" + std::string(io.KeyShift ? "S-" : "") + mods + name + ">";
            continue;
        }
        // Letters and digits with Ctrl or Alt arrive as keys, not as text.
        if ((io.KeyCtrl || io.KeyAlt) && !(io.KeyCtrl && key == ImGuiKey_B)) {
            char ch = 0;
            if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
                ch = static_cast<char>('a' + (key - ImGuiKey_A));
            } else if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
                ch = static_cast<char>('0' + (key - ImGuiKey_0));
            }
            if (ch != 0) {
                keys += "<" + std::string(io.KeyShift ? "S-" : "") + mods + std::string(1, ch) + ">";
            }
        }
    }
    // Typed text, Cyrillic included, as UTF-8; "<" is spelled out.
    if (!io.KeyCtrl && !io.KeyAlt) {
        for (const ImWchar ch : io.InputQueueCharacters) {
            if (ch == '<') {
                keys += "<lt>";
                continue;
            }
            rv_editor_utf8_append(keys, ch);
        }
    }
    return keys;
}

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
        text.push_back(std::filesystem::path(name).filename().string() + (held(name)->modified ? " [+]" : "") + "##" +
            name);
    }
    if (front != nullptr && shown.empty()) {
        active = static_cast<int>(text.size());
        text.push_back(std::string("Untitled") + (front->modified ? " [+]" : ""));
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
        const char *why_not = !named                   ? "An Untitled document has no tab to close; save it first."
            : tabs.names.size() < 2                    ? "The tile's only file stays in front."
            : held(tabs.names[static_cast<size_t>(active)])->modified ? "The file has unsaved changes; save it first."
                                                       : nullptr;
        if (ImGui::MenuItem("Close Tab", nullptr, false, why_not == nullptr)) {
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
        nvim.ensure_started(app.project.open ? app.project.root : std::filesystem::current_path(), app.log);
    }
    if (!nvim.problem.empty()) {
        ImGui::TextWrapped("%s", nvim.problem.c_str());
        if (rv_editor_button("Start nvim Again", theme)) {
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
    if (rv_editor_toggle("Vim##mode", &vim, theme)) {
        nvim.toggle_vim_mode();
    }
    ImGui::SetItemTooltip("Full Vim: normal mode and Vim keys. Off: an ordinary editor. F2 switches too.");
    rv_editor_code_tab_row(app, pane, win, theme);
    rv_editor_shelf_end();
    rv_editor_font_code_push();
    rv_editor_well_begin("##well", ImVec2(0, 0), theme, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // The code area fills the tile in whole cells, two rows kept under it: the
    // tile's status line and the command line.
    const ImVec2 cell(ImGui::CalcTextSize("M").x, ImGui::GetTextLineHeight());
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int32_t cols = std::max(1, static_cast<int32_t>(avail.x / cell.x));
    const int32_t rows = std::max(3, static_cast<int32_t>(avail.y / cell.y)) - 2;
    nvim.resize(win, cols, rows);

    ImGui::InvisibleButton("##code", ImVec2(cols * cell.x, (rows + 2) * cell.y));
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
            nvim.mouse("left", "press", grid_id, row, col);
        } else if (ImGui::IsItemActive() && (row != last_row || col != last_col)) {
            nvim.mouse("left", "drag", grid_id, row, col);
        } else if (ImGui::IsItemDeactivated()) {
            nvim.mouse("left", "release", grid_id, row, col);
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
            const char *dir = sideways ? (amount > 0.0f ? "left" : "right") : (amount > 0.0f ? "up" : "down");
            for (int i = 0; amount != 0.0f && i < std::max(1, notches); ++i) {
                nvim.mouse("wheel", dir, grid_id, row, col);
            }
        }
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + cols * cell.x, at.y + (rows + 2) * cell.y), IM_COL32(0x1e, 0x1e, 0x2e, 255));
    if (grid == nullptr) {
        dl->AddText(at, IM_COL32(0xa6, 0xad, 0xc8, 255), "Starting nvim...");
        rv_editor_well_end();
        return;
    }
    const bool cursor_here = focused && nvim.screen().cursor_grid() == grid_id;
    rv_editor_nvim_draw_grid(nvim.screen(), *grid, at, cell, rows, cursor_here);

    // The tile's status line: the file, and whether it is saved (TXT-07).
    const ImVec2 status(at.x, at.y + rows * cell.y);
    dl->AddRectFilled(status, ImVec2(at.x + cols * cell.x, status.y + cell.y), IM_COL32(0x45, 0x47, 0x5a, 255));
    std::string label = "Untitled";
    if (const rv_editor_nvim_buffer *buf = nvim.buffer_in(win)) {
        std::error_code ec;
        const std::filesystem::path rel = app.project.open && !buf->name.empty()
            ? std::filesystem::relative(buf->name, app.project.root, ec)
            : std::filesystem::path(buf->name);
        label = buf->name.empty() ? "Untitled" : (ec || rel.empty() ? buf->name : rel.string());
        label += buf->modified ? " [+]" : "";
    }
    dl->AddText(ImVec2(status.x + cell.x, status.y), IM_COL32(0xcd, 0xd6, 0xf4, 255), label.c_str());

    // LSP note: nothing when running, otherwise why diagnostics/completion are
    // absent for this file type, with the exact reason as a tooltip.
    float note_x = status.x + cell.x * 2 + ImGui::CalcTextSize(label.c_str()).x;
    if (const rv_editor_nvim_buffer *buf = nvim.buffer_in(win); buf != nullptr && !buf->name.empty()) {
        const std::string server = nvim.lsp_server_for(buf->name);
        const rv_editor_nvim_lsp *lsp = server.empty() ? nullptr : nvim.lsp_status(server);
        if (lsp != nullptr && lsp->state != "running") {
            const std::string note =
                "LSP: " + server + " " + (lsp->state == "missing" ? "not running" : "stopped");
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
            text = "Reload: entry script";
            break;
        case rv_editor_change_action::reload_module:
            owned = "Reload: module " + plan.name;
            text = owned.c_str();
            break;
        case rv_editor_change_action::refresh_texture:
            owned = "Refresh: texture " + plan.name;
            text = owned.c_str();
            break;
        case rv_editor_change_action::build_restart:
        case rv_editor_change_action::restart_required:
            text = "Build and Restart";
            warn = true;
            break;
        case rv_editor_change_action::not_in_disc:
            text = "Not on the running disc";
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
