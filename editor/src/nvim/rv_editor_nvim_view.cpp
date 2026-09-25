// A code tile: one nvim window's grid drawn with the code font, and the
// keyboard turned into nvim_input notation while the tile has focus.

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <string>

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
    // A block in normal mode, a bar while inserting.
    const rv_editor_nvim_cell &under = grid.cells[static_cast<size_t>(grid.cursor_row * grid.width + grid.cursor_col)];
    uint32_t fg = 0;
    uint32_t bg = 0;
    screen.colors(under.hl, fg, bg);
    const ImVec2 p0(at.x + grid.cursor_col * cell.x, at.y + grid.cursor_row * cell.y);
    const bool bar = screen.mode().starts_with("insert") || screen.mode().starts_with("cmdline");
    if (bar) {
        dl->AddRectFilled(p0, ImVec2(p0.x + std::max(2.0f, cell.x / 5.0f), p0.y + cell.y), rv_editor_rgb(fg));
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
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + cols * cell.x, at.y + (rows + 2) * cell.y), IM_COL32(0x1e, 0x1e, 0x2e, 255));
    if (grid == nullptr) {
        dl->AddText(at, IM_COL32(0xa6, 0xad, 0xc8, 255), "Starting nvim...");
        return;
    }
    const bool cursor_here = focused && nvim.screen().cursor_grid() == grid_id;
    rv_editor_nvim_draw_grid(nvim.screen(), *grid, at, cell, rows, cursor_here);

    // The tile's status line: the file, and whether it is saved (TXT-07).
    const ImVec2 status(at.x, at.y + rows * cell.y);
    dl->AddRectFilled(status, ImVec2(at.x + cols * cell.x, status.y + cell.y), IM_COL32(0x45, 0x47, 0x5a, 255));
    std::string label = "[No Name]";
    if (const rv_editor_nvim_buffer *buf = nvim.buffer_in(win)) {
        std::error_code ec;
        const std::filesystem::path rel = app.project.open && !buf->name.empty()
            ? std::filesystem::relative(buf->name, app.project.root, ec)
            : std::filesystem::path(buf->name);
        label = buf->name.empty() ? "[No Name]" : (ec || rel.empty() ? buf->name : rel.string());
        label += buf->modified ? " [+]" : "";
    }
    dl->AddText(ImVec2(status.x + cell.x, status.y), IM_COL32(0xcd, 0xd6, 0xf4, 255), label.c_str());

    if (!focused) {
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
}

} // namespace

} // namespace rv_editor
