#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include <vterm_keycodes.h>

#include "platform/rv_editor_pty.hpp"
#include "theme/rv_editor_theme.hpp"

struct VTerm;
struct VTermScreen;

namespace rv_editor
{

// One character cell as the Terminal tile draws it, its colours already 0xRRGGBB.
struct rv_editor_term_cell
{
    std::string text; // UTF-8; empty for a blank and for a wide character's second cell
    uint32_t fg = 0;
    uint32_t bg = 0;
    bool bold = false;
    bool underline = false;
};

using rv_editor_term_line = std::vector<rv_editor_term_cell>;

// Classic VT100 terminal size (80x24) that programs assume before the first resize arrives
constexpr int terminal_default_cols = 80;
constexpr int terminal_default_rows = 24;

// A shell on a PTY with its screen kept by libvterm: ANSI colours in the
// code area's Mocha, resize, Ctrl+C through the terminal, and the lines that
// scrolled off the top kept up to scrollback_max. Each terminal is a session of
// its own; none sees the dev protocol.
class rv_editor_terminal
{
public:
    static constexpr size_t scrollback_max = 5000;

    rv_editor_terminal() = default;
    rv_editor_terminal(const rv_editor_terminal &) = delete;
    rv_editor_terminal &operator=(const rv_editor_terminal &) = delete;
    ~rv_editor_terminal();

    // The user's shell ($SHELL, else /bin/sh) in `cwd`, on cols x rows, in the
    // theme's code colours. RV_OK or RV_ERR_* with the reason in `error`.
    int start(const std::filesystem::path &cwd, int cols, int rows, const rv_editor_theme &theme,
        std::string &error);
    bool running() const { return pty_.running(); }
    // How the shell ended ("exit code 0"), empty while it runs.
    std::string ended() const;

    // Once a frame: what the shell wrote goes through the emulator, and keys
    // queued for it are written.
    void update();
    void resize(int cols, int rows);
    int cols() const { return cols_; }
    int rows() const { return rows_; }

    // Keys for the shell: a typed character, or a named key.
    void text(uint32_t codepoint, VTermModifier mod);
    void key(VTermKey key, VTermModifier mod);

    // The screen now, row by row, and the lines above it, oldest first.
    rv_editor_term_line line(int row) const;
    const std::deque<rv_editor_term_line> &scrollback() const { return scrollback_; }
    int cursor_row() const { return cursor_row_; }
    int cursor_col() const { return cursor_col_; }
    bool cursor_visible() const { return cursor_visible_; }

    // For the emulator's callbacks (rv_editor_terminal.cpp).
    struct hooks;

private:
    rv_editor_term_cell convert(const void *cell) const;

    rv_editor_pty pty_;
    VTerm *vt_ = nullptr;
    VTermScreen *screen_ = nullptr;
    std::deque<rv_editor_term_line> scrollback_;
    int cols_ = terminal_default_cols;
    int rows_ = terminal_default_rows;
    int cursor_row_ = 0;
    int cursor_col_ = 0;
    bool cursor_visible_ = true;
};

} // namespace rv_editor
