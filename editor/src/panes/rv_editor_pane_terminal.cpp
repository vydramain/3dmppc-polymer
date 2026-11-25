// A Terminal tile: its shell's screen drawn in the code font on the code area's
// Mocha, and the keyboard sent to the shell while the tile has focus.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "app/rv_editor_shell.hpp"
#include "font/rv_editor_font.hpp"

#include "panes/rv_editor_pane_tiled.hpp"
#include "panes/rv_editor_panes.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// UTF-8 single-byte lead threshold.
constexpr uint8_t utf8_1byte_limit = 0x80;
// UTF-8 two-byte lead byte pattern check (bits 7:5 = 0b110).
constexpr uint8_t utf8_2byte_lead_check = 0x6;
// UTF-8 three-byte lead byte pattern check (bits 7:4 = 0b1110).
constexpr uint8_t utf8_3byte_lead_check = 0xe;
// UTF-8 four-byte lead byte pattern check (bits 7:3 = 0b11110).
constexpr uint8_t utf8_4byte_lead_check = 0x1e;
// UTF-8 initial byte mask after lead pattern.
constexpr uint8_t utf8_initial_mask = 0x7f;
// UTF-8 continuation byte bits per character.
constexpr int utf8_continuation_bits = 6;
// UTF-8 continuation byte mask (0b00111111).
constexpr uint8_t utf8_continuation_mask = 0x3f;
// UTF-8 lead byte pattern detection: shifts to align lead pattern bits (RFC 3629).
constexpr int utf8_2byte_lead_shift = 5; // Check bits 7:5 match utf8_2byte_lead_check
constexpr int utf8_3byte_lead_shift = 4; // Check bits 7:4 match utf8_3byte_lead_check
constexpr int utf8_4byte_lead_shift = 3; // Check bits 7:3 match utf8_4byte_lead_check
// UTF-8 byte sequence lengths decoded from lead byte pattern (RFC 3629).
constexpr int utf8_2byte_length = 2;
constexpr int utf8_3byte_length = 3;
constexpr int utf8_4byte_length = 4;
// Grid cell width probe glyph for layout measurement (editor assumption: monospace font).
constexpr const char *grid_cell_width_probe = "M";
// Minimum terminal grid columns.
constexpr int min_terminal_cols = 2;
// Minimum terminal grid rows.
constexpr int min_terminal_rows = 3;
// Scrollback lines per mouse wheel notch.
constexpr int scroll_lines_per_notch = 3;

ImU32 rv_editor_rgb(uint32_t rgb)
{
    return IM_COL32((rgb >> channel_shift_red) & rgb_channel_mask,
        (rgb >> channel_shift_green) & rgb_channel_mask,
        rgb & rgb_channel_mask,
        rv_editor::alpha_opaque);
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
            dl->AddRectFilled(ImVec2(at.x + start * cell.x, at.y), ImVec2(at.x + c * cell.x, at.y + cell.y), rv_editor_rgb(bg));
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

// Named keys in libvterm's terms.
VTermKey rv_editor_term_key(ImGuiKey key)
{
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) {
        return static_cast<VTermKey>(VTERM_KEY_FUNCTION(key - ImGuiKey_F1 + 1));
    }
    switch (key) {
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter:
        return VTERM_KEY_ENTER;
    case ImGuiKey_Tab:
        return VTERM_KEY_TAB;
    case ImGuiKey_Backspace:
        return VTERM_KEY_BACKSPACE;
    case ImGuiKey_Escape:
        return VTERM_KEY_ESCAPE;
    case ImGuiKey_UpArrow:
        return VTERM_KEY_UP;
    case ImGuiKey_DownArrow:
        return VTERM_KEY_DOWN;
    case ImGuiKey_LeftArrow:
        return VTERM_KEY_LEFT;
    case ImGuiKey_RightArrow:
        return VTERM_KEY_RIGHT;
    case ImGuiKey_Insert:
        return VTERM_KEY_INS;
    case ImGuiKey_Delete:
        return VTERM_KEY_DEL;
    case ImGuiKey_Home:
        return VTERM_KEY_HOME;
    case ImGuiKey_End:
        return VTERM_KEY_END;
    case ImGuiKey_PageUp:
        return VTERM_KEY_PAGEUP;
    case ImGuiKey_PageDown:
        return VTERM_KEY_PAGEDOWN;
    default:
        return VTERM_KEY_NONE;
    }
}

// UTF-8 text to the shell as if typed.
void rv_editor_term_type(rv_editor_terminal &term, const std::string &text)
{
    size_t i = 0;
    while (i < text.size()) {
        // One code point; a byte that starts none is skipped.
        const auto b = static_cast<unsigned char>(text[i]);
        const size_t n = b < utf8_1byte_limit                     ? 1 :
            (b >> utf8_2byte_lead_shift) == utf8_2byte_lead_check ? utf8_2byte_length :
            (b >> utf8_3byte_lead_shift) == utf8_3byte_lead_check ? utf8_3byte_length :
            (b >> utf8_4byte_lead_shift) == utf8_4byte_lead_check ? utf8_4byte_length :
                                                                    0;
        if (n == 0 || i + n > text.size()) {
            ++i;
            continue;
        }
        uint32_t ch = n == 1 ? b : b & (utf8_initial_mask >> n);
        for (size_t k = 1; k < n; ++k) {
            ch = (ch << utf8_continuation_bits) | (static_cast<unsigned char>(text[i + k]) & utf8_continuation_mask);
        }
        i += n;
        if (ch == '\n') {
            term.key(VTERM_KEY_ENTER, VTERM_MOD_NONE);
        } else if (ch != '\r') {
            term.text(ch, VTERM_MOD_NONE);
        }
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
        // Ctrl+C is the interrupt.
        if (io.KeyCtrl || io.KeyAlt) {
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

// The widest row: the message box with the widest of its button rows.
float rv_editor_terminal_content_width()
{
    return rv_editor_message_strip_min_width(
        { { rv_editor_text("shell_menu.ask_terminal_end"), rv_editor_text("shell_menu.ask_terminal_keep") },
            { rv_editor_text("pane_terminal.paste"), rv_editor_text("pane_terminal.cancel") },
            { rv_editor_text("pane_terminal.start_again") } });
}

// What the terminal says in the strip under the screen, whatever it takes: the shell's end question, several pasted lines to
// look at, or that the shell ended or never started. Each has its buttons; a running shell with nothing to ask says nothing
// and has no strip.
rv_editor_message_say rv_editor_terminal_say(const rv_editor_terminal_view &view)
{
    if (view.ending) {
        return { rv_editor_text("shell_menu.ask_terminal_message"),
            rv_editor_label_kind::normal,
            { rv_editor_text("shell_menu.ask_terminal_end"), rv_editor_text("shell_menu.ask_terminal_keep") } };
    }
    if (!view.paste.empty() && view.term != nullptr) {
        // Every pasted line is in the strip, to look at before it runs.
        return { std::string(rv_editor_text("pane_terminal.paste_lines_info")) + "\n" + view.paste,
            rv_editor_label_kind::normal,
            { rv_editor_text("pane_terminal.paste"), rv_editor_text("pane_terminal.cancel") } };
    }
    std::string why;
    if (!view.error.empty()) {
        why = rv_editor_text_format("pane_terminal.shell_not_started", std::make_format_args(view.error));
    } else if (view.term != nullptr && !view.term->running()) {
        const std::string ended_msg = view.term->ended();
        why = rv_editor_text_format("pane_terminal.shell_ended", std::make_format_args(ended_msg));
    }
    return { why,
        view.error.empty() ? rv_editor_label_kind::normal : rv_editor_label_kind::error,
        why.empty() ? std::vector<std::string>{} : std::vector<std::string>{ rv_editor_text("pane_terminal.start_again") } };
}

// What the pressed button of that strip does.
void rv_editor_terminal_answer(rv_editor_terminal_view &view, int pressed)
{
    if (pressed < 0) {
        return;
    }
    if (view.ending) {
        view.end_answer = pressed == 0 ? rv_editor_terminal_end_answer::end : rv_editor_terminal_end_answer::keep;
    } else if (!view.paste.empty() && view.term != nullptr) {
        if (pressed == 0) {
            rv_editor_term_type(*view.term, view.paste);
        }
        view.paste.clear();
    } else if (pressed == 0) {
        view = {};
    }
}

// The shell's screen in the content area, in whole cells of the code font. Starts the shell at the area's size.
void rv_editor_terminal_screen(rv_editor_app &app,
    rv_editor_terminal_view &view,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui)
{
    // The screen fills the tile in whole cells: no permanent status row.
    const ImVec2 cell(ImGui::CalcTextSize(grid_cell_width_probe).x, ImGui::GetTextLineHeight());
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int cols = std::max(min_terminal_cols, static_cast<int>(avail.x / cell.x));
    const int rows = std::max(min_terminal_rows, static_cast<int>(avail.y / cell.y));
    if (view.term == nullptr && view.error.empty()) {
        view.term = std::make_unique<rv_editor_terminal>();
        view.cwd = app.project.open ? app.project.root : std::filesystem::current_path();
        if (view.term->start(view.cwd, cols, rows, theme, view.error) != RV_OK && view.error.empty()) {
            view.error = rv_editor_text("pane_terminal.unknown_error");
        }
    }
    if (view.term == nullptr) {
        return; // it did not start: the box says why
    }
    rv_editor_terminal &term = *view.term;
    if (term.running()) {
        term.resize(cols, rows);
    }

    ImGui::InvisibleButton("##terminal", ImVec2(cols * cell.x, rows * cell.y));
    if (view.focus_request) {
        view.focus_request = false;
        ImGui::SetWindowFocus();
    }
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    const auto &back = term.scrollback();
    // The wheel scrolls back through the lines above the screen, three a notch.
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        const int notches = static_cast<int>(std::lround(ImGui::GetIO().MouseWheel));
        view.scroll += (notches == 0 ? (ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1) : notches) * scroll_lines_per_notch;
    }
    view.scroll = std::clamp(view.scroll, 0, static_cast<int>(back.size()));

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + cols * cell.x, at.y + rows * cell.y), rv_editor_rgb(theme.code_base));
    const int shown = std::min(rows, term.rows());
    for (int r = 0; r < shown; ++r) {
        const int from_screen = r - view.scroll; // negative: a line above the screen
        const rv_editor_term_line line =
            from_screen < 0 ? back[back.size() + static_cast<size_t>(from_screen)] : term.line(from_screen);
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

    // Only while the view is scrolled back: how far, over the last row. The grid's
    // size is in the tooltip, not on screen.
    if (view.scroll > 0) {
        const ImVec2 status(at.x, at.y + (rows - 1) * cell.y);
        dl->AddRectFilled(status, ImVec2(at.x + cols * cell.x, status.y + cell.y), rv_editor_rgb(theme.code_surface));
        const int scroll_count = view.scroll;
        const std::string label =
            rv_editor_text_format("pane_terminal.scroll_back_status", std::make_format_args(scroll_count));
        dl->AddText(ImVec2(status.x + cell.x, status.y), rv_editor_rgb(theme.code_text), label.c_str());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        const std::string cwd_text = view.cwd.string();
        const int grid_cols = term.cols();
        const int grid_rows = term.rows();
        const std::string size_tip =
            rv_editor_text_format("pane_terminal.size_tooltip", std::make_format_args(cwd_text, grid_cols, grid_rows));
        rv_editor_tooltip(size_tip.c_str());
    }

    if (!focused || !term.running()) {
        return;
    }
    // Keys go to the shell; ImGui's own keyboard navigation stays off meanwhile.
    ui.requests.push_back({ .kind = rv_editor_ui_request_kind::take_text_focus, .focus = true });
    if (SDL_Window *w = SDL_GetKeyboardFocus(); w != nullptr && !SDL_TextInputActive(w)) {
        SDL_StartTextInput(w);
    }
    // Ctrl+Shift+V pastes; one line goes straight in, several wait for a look.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V)) {
        const char *clip = ImGui::GetClipboardText();
        const std::string text = clip != nullptr ? clip : "";
        if (text.find('\n') == std::string::npos) {
            rv_editor_term_type(term, text);
        } else {
            view.paste = text;
        }
        view.scroll = 0;
        return;
    }
    if (rv_editor_term_keys(term)) {
        view.scroll = 0;
    }
}

} // namespace

rv_editor_size rv_editor_terminal_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_terminal_content_width(), rv_editor_tiled_strip_block());
}

void rv_editor_pane_terminal(rv_editor_app &app,
    rv_editor_pane_id pane,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui)
{
    rv_editor_terminal_view &view = app.terminals[pane];
    const rv_editor_message_say say = rv_editor_terminal_say(view);
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, rv_editor_terminal_content_width());
    const float strip_h = rv_editor_message_strip_height(say.text, say.buttons, strip_w);
    // The area is measured in the UI font like the minimum; the screen in it is in the code font.
    (void)rv_editor_tiled_area_begin("##screen", rv_editor_terminal_content_width(), theme, {}, false, 0.0f, strip_h);
    rv_editor_font_code_push();
    rv_editor_terminal_screen(app, view, theme, ui);
    rv_editor_font_code_pop();
    rv_editor_tiled_area_end(theme);
    rv_editor_terminal_answer(view, rv_editor_message_strip("##strip", say.text, say.kind, say.buttons, theme, strip_w));
}

} // namespace rv_editor
