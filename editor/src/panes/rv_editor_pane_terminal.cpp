// A Terminal tile: its shell's screen drawn in the code font on the code area's
// Mocha, and the keyboard sent to the shell while the tile has focus (TRM-01).

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
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

// One line of cells: backgrounds other than the area's own in runs, then each
// cell's character at its own column, so a wide character keeps the grid.
void rv_editor_term_draw_line(ImDrawList *dl, const rv_editor_term_line &line, ImVec2 at, ImVec2 cell, uint32_t base)
{
    size_t c = 0;
    while (c < line.size()) {
        const uint32_t bg = line[c].bg;
        const size_t start = c;
        while (c < line.size() && line[c].bg == bg) {
            ++c;
        }
        if (bg != base) {
            dl->AddRectFilled(ImVec2(at.x + start * cell.x, at.y), ImVec2(at.x + c * cell.x, at.y + cell.y),
                rv_editor_rgb(bg));
        }
    }
    for (c = 0; c < line.size(); ++c) {
        const rv_editor_term_cell &k = line[c];
        const ImVec2 p(at.x + c * cell.x, at.y);
        if (!k.text.empty() && k.text != " ") {
            dl->AddText(p, rv_editor_rgb(k.fg), k.text.c_str());
        }
        if (k.underline) {
            dl->AddLine(ImVec2(p.x, p.y + cell.y - 1), ImVec2(p.x + cell.x, p.y + cell.y - 1), rv_editor_rgb(k.fg));
        }
    }
}

// Named keys in libvterm's terms; F5, F6 and F7 stay the editor's (section 12).
VTermKey rv_editor_term_key(ImGuiKey key)
{
    switch (key) {
        case ImGuiKey_Enter:
        case ImGuiKey_KeypadEnter: return VTERM_KEY_ENTER;
        case ImGuiKey_Tab: return VTERM_KEY_TAB;
        case ImGuiKey_Backspace: return VTERM_KEY_BACKSPACE;
        case ImGuiKey_Escape: return VTERM_KEY_ESCAPE;
        case ImGuiKey_UpArrow: return VTERM_KEY_UP;
        case ImGuiKey_DownArrow: return VTERM_KEY_DOWN;
        case ImGuiKey_LeftArrow: return VTERM_KEY_LEFT;
        case ImGuiKey_RightArrow: return VTERM_KEY_RIGHT;
        case ImGuiKey_Insert: return VTERM_KEY_INS;
        case ImGuiKey_Delete: return VTERM_KEY_DEL;
        case ImGuiKey_Home: return VTERM_KEY_HOME;
        case ImGuiKey_End: return VTERM_KEY_END;
        case ImGuiKey_PageUp: return VTERM_KEY_PAGEUP;
        case ImGuiKey_PageDown: return VTERM_KEY_PAGEDOWN;
        case ImGuiKey_F1: return static_cast<VTermKey>(VTERM_KEY_FUNCTION(1));
        case ImGuiKey_F2: return static_cast<VTermKey>(VTERM_KEY_FUNCTION(2));
        case ImGuiKey_F3: return static_cast<VTermKey>(VTERM_KEY_FUNCTION(3));
        case ImGuiKey_F4: return static_cast<VTermKey>(VTERM_KEY_FUNCTION(4));
        case ImGuiKey_F8: return static_cast<VTermKey>(VTERM_KEY_FUNCTION(8));
        case ImGuiKey_F9: return static_cast<VTermKey>(VTERM_KEY_FUNCTION(9));
        case ImGuiKey_F10: return static_cast<VTermKey>(VTERM_KEY_FUNCTION(10));
        case ImGuiKey_F11: return static_cast<VTermKey>(VTERM_KEY_FUNCTION(11));
        case ImGuiKey_F12: return static_cast<VTermKey>(VTERM_KEY_FUNCTION(12));
        default: return VTERM_KEY_NONE;
    }
}

// This frame's keyboard to the shell. Returns true when anything was typed.
bool rv_editor_term_keys(rv_editor_terminal &term)
{
    const ImGuiIO &io = ImGui::GetIO();
    int mods = VTERM_MOD_NONE;
    if (io.KeyShift) {
        mods |= VTERM_MOD_SHIFT;
    }
    if (io.KeyCtrl) {
        mods |= VTERM_MOD_CTRL;
    }
    if (io.KeyAlt) {
        mods |= VTERM_MOD_ALT;
    }
    const VTermModifier mod = static_cast<VTermModifier>(mods);
    bool typed = false;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const ImGuiKey key = static_cast<ImGuiKey>(k);
        if (!ImGui::IsKeyPressed(key, true)) {
            continue;
        }
        if (const VTermKey named = rv_editor_term_key(key); named != VTERM_KEY_NONE) {
            term.key(named, mod);
            typed = true;
            continue;
        }
        // Ctrl or Alt with a letter or digit arrives as a key, not as text:
        // Ctrl+C is the interrupt. Ctrl+B stays Build.
        if ((io.KeyCtrl || io.KeyAlt) && !(io.KeyCtrl && key == ImGuiKey_B)) {
            uint32_t ch = 0;
            if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
                ch = 'a' + static_cast<uint32_t>(key - ImGuiKey_A);
            } else if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
                ch = '0' + static_cast<uint32_t>(key - ImGuiKey_0);
            }
            if (ch != 0) {
                term.text(ch, static_cast<VTermModifier>(mods & ~VTERM_MOD_SHIFT));
                typed = true;
            }
        }
    }
    if (!io.KeyCtrl && !io.KeyAlt) {
        for (const ImWchar ch : io.InputQueueCharacters) {
            term.text(ch, VTERM_MOD_NONE);
            typed = true;
        }
    }
    return typed;
}

void rv_editor_pane_terminal_body(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    rv_editor_terminal_view &view = app.terminals[pane];

    // A shell that ended or never started says so above the screen it left.
    if (view.term == nullptr || !view.term->running() || !view.error.empty()) {
        const std::string why = !view.error.empty()       ? "The shell did not start: " + view.error
            : view.term != nullptr && !view.term->running() ? "The shell ended: " + view.term->ended() + "."
                                                            : std::string();
        if (!why.empty()) {
            rv_editor_font_code_pop();
            ImGui::TextUnformatted(why.c_str());
            ImGui::SameLine();
            if (rv_editor_button("Start Again", theme)) {
                view = {};
            }
            rv_editor_font_code_push();
        }
    }

    // The screen fills the tile in whole cells, one row kept under it for the
    // tile's status line.
    const ImVec2 cell(ImGui::CalcTextSize("M").x, ImGui::GetTextLineHeight());
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int cols = std::max(2, static_cast<int>(avail.x / cell.x));
    const int rows = std::max(3, static_cast<int>(avail.y / cell.y)) - 1;
    if (view.term == nullptr && view.error.empty()) {
        view.term = std::make_unique<rv_editor_terminal>();
        const std::filesystem::path cwd = app.project.open ? app.project.root : std::filesystem::current_path();
        if (!view.term->start(cwd, cols, rows, theme, view.error) && view.error.empty()) {
            view.error = "unknown error";
        }
    }
    rv_editor_terminal &term = *view.term;
    if (term.running()) {
        term.resize(cols, rows);
    }

    ImGui::InvisibleButton("##terminal", ImVec2(cols * cell.x, (rows + 1) * cell.y));
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    const auto &back = term.scrollback();
    // The wheel scrolls back through the lines above the screen, three a notch.
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        const int notches = static_cast<int>(std::lround(ImGui::GetIO().MouseWheel));
        view.scroll += (notches == 0 ? (ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1) : notches) * 3;
    }
    view.scroll = std::clamp(view.scroll, 0, static_cast<int>(back.size()));

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + cols * cell.x, at.y + rows * cell.y), rv_editor_rgb(theme.code_base));
    const int shown = std::min(rows, term.rows());
    for (int r = 0; r < shown; ++r) {
        const int from_screen = r - view.scroll; // negative: a line above the screen
        const rv_editor_term_line line = from_screen < 0
            ? back[back.size() + static_cast<size_t>(from_screen)]
            : term.line(from_screen);
        rv_editor_term_draw_line(dl, line, ImVec2(at.x, at.y + r * cell.y), cell, theme.code_base);
    }
    const int cursor_r = term.cursor_row() + view.scroll;
    if (term.running() && term.cursor_visible() && cursor_r < shown) {
        const ImVec2 p0(at.x + term.cursor_col() * cell.x, at.y + cursor_r * cell.y);
        const ImVec2 p1(p0.x + cell.x, p0.y + cell.y);
        if (focused) {
            dl->AddRectFilled(p0, p1, rv_editor_rgb(theme.code_text));
            const rv_editor_term_line line = term.line(term.cursor_row());
            if (term.cursor_col() < static_cast<int>(line.size())) {
                dl->AddText(p0, rv_editor_rgb(theme.code_base), line[term.cursor_col()].text.c_str());
            }
        } else {
            dl->AddRect(p0, p1, rv_editor_rgb(theme.code_subtext));
        }
    }

    // The tile's status line: the size, and how far back the view is.
    const ImVec2 status(at.x, at.y + rows * cell.y);
    dl->AddRectFilled(status, ImVec2(at.x + cols * cell.x, status.y + cell.y), rv_editor_rgb(theme.code_surface));
    std::string label = std::to_string(term.cols()) + "x" + std::to_string(term.rows());
    if (view.scroll > 0) {
        label += "  " + std::to_string(view.scroll) + " lines back";
    }
    dl->AddText(ImVec2(status.x + cell.x, status.y), rv_editor_rgb(theme.code_text), label.c_str());

    if (!focused || !term.running()) {
        return;
    }
    // Keys go to the shell; ImGui's own keyboard navigation stays off meanwhile.
    app.text_focus = true;
    if (SDL_Window *w = SDL_GetKeyboardFocus(); w != nullptr && !SDL_TextInputActive(w)) {
        SDL_StartTextInput(w);
    }
    if (rv_editor_term_keys(term)) {
        view.scroll = 0;
    }
}

} // namespace

void rv_editor_pane_terminal(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    rv_editor_font_code_push();
    rv_editor_pane_terminal_body(app, pane, theme);
    rv_editor_font_code_pop();
}

} // namespace rv_editor
