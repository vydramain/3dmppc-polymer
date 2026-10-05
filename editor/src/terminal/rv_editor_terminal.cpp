// The Terminal tile's model: libvterm between the PTY and the drawing.

#include "terminal/rv_editor_terminal.hpp"

#include <algorithm>
#include <cstdlib>

#include <vterm.h>

namespace rv_editor
{

namespace
{

VTermColor rv_editor_vterm_rgb(uint32_t rgb)
{
    VTermColor c;
    vterm_color_rgb(&c, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
    return c;
}

void rv_editor_utf8(std::string &out, uint32_t cp)
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

} // namespace

struct rv_editor_terminal::hooks
{
    // What the emulator answers the shell (cursor reports, key sequences).
    static void output(const char *s, size_t len, void *user)
    {
        static_cast<rv_editor_terminal *>(user)->pty_.write(std::string_view(s, len));
    }

    static int pushline(int cols, const VTermScreenCell *cells, void *user)
    {
        auto &t = *static_cast<rv_editor_terminal *>(user);
        rv_editor_term_line line;
        line.reserve(static_cast<size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            line.push_back(t.convert(&cells[c]));
        }
        t.scrollback_.push_back(std::move(line));
        if (t.scrollback_.size() > scrollback_max) {
            t.scrollback_.pop_front();
        }
        return 1;
    }

    static int sb_clear(void *user)
    {
        static_cast<rv_editor_terminal *>(user)->scrollback_.clear();
        return 1;
    }

    static int movecursor(VTermPos pos, VTermPos, int visible, void *user)
    {
        auto &t = *static_cast<rv_editor_terminal *>(user);
        t.cursor_row_ = pos.row;
        t.cursor_col_ = pos.col;
        t.cursor_visible_ = visible != 0;
        return 1;
    }

    static int settermprop(VTermProp prop, VTermValue *val, void *user)
    {
        if (prop == VTERM_PROP_CURSORVISIBLE) {
            static_cast<rv_editor_terminal *>(user)->cursor_visible_ = val->boolean != 0;
        }
        return 1;
    }
};

rv_editor_terminal::~rv_editor_terminal()
{
    pty_.stop();
    if (vt_ != nullptr) {
        vterm_free(vt_);
    }
}

bool rv_editor_terminal::start(const std::filesystem::path &cwd, int cols, int rows, const rv_editor_theme &theme,
    std::string &error)
{
    cols_ = std::max(cols, 2);
    rows_ = std::max(rows, 2);
    if (vt_ == nullptr) {
        vt_ = vterm_new(rows_, cols_);
        vterm_set_utf8(vt_, 1);
        vterm_output_set_callback(vt_, &hooks::output, this);
        screen_ = vterm_obtain_screen(vt_);
        static const VTermScreenCallbacks callbacks = {
            .damage = nullptr,
            .moverect = nullptr,
            .movecursor = &hooks::movecursor,
            .settermprop = &hooks::settermprop,
            .bell = nullptr,
            .resize = nullptr,
            .sb_pushline = &hooks::pushline,
            .sb_popline = nullptr,
            .sb_clear = &hooks::sb_clear,
        };
        vterm_screen_set_callbacks(screen_, &callbacks, this);
        vterm_screen_enable_altscreen(screen_, 1);
        // The ANSI colours from the code area's palette.
        const uint32_t palette[16] = {
            theme.code_surface, theme.code_red, theme.code_green, theme.code_yellow,
            theme.code_blue, theme.code_magenta, theme.code_cyan, theme.code_subtext,
            theme.code_surface, theme.code_red, theme.code_green, theme.code_yellow,
            theme.code_blue, theme.code_magenta, theme.code_cyan, theme.code_text,
        };
        VTermState *state = vterm_obtain_state(vt_);
        for (int i = 0; i < 16; ++i) {
            const VTermColor c = rv_editor_vterm_rgb(palette[i]);
            vterm_state_set_palette_color(state, i, &c);
        }
        const VTermColor fg = rv_editor_vterm_rgb(theme.code_text);
        const VTermColor bg = rv_editor_vterm_rgb(theme.code_base);
        vterm_screen_set_default_colors(screen_, &fg, &bg);
        vterm_screen_reset(screen_, 1);
    } else {
        vterm_set_size(vt_, rows_, cols_);
    }

    const char *shell = std::getenv("SHELL");
    const std::string program = shell != nullptr && shell[0] == '/' ? shell : "/bin/sh";
    return pty_.start({ program }, cwd, cols_, rows_, error);
}

std::string rv_editor_terminal::ended() const
{
    return running() ? std::string() : rv_editor_exit_text(pty_.exit_status());
}

void rv_editor_terminal::update()
{
    if (vt_ == nullptr) {
        return;
    }
    std::string bytes;
    pty_.read(bytes, 1 << 20);
    if (!bytes.empty()) {
        vterm_input_write(vt_, bytes.data(), bytes.size());
        vterm_screen_flush_damage(screen_);
    }
    pty_.flush();
    pty_.poll();
}

void rv_editor_terminal::resize(int cols, int rows)
{
    cols = std::max(cols, 2);
    rows = std::max(rows, 2);
    if (vt_ == nullptr || (cols == cols_ && rows == rows_)) {
        return;
    }
    cols_ = cols;
    rows_ = rows;
    vterm_set_size(vt_, rows_, cols_);
    pty_.resize(cols_, rows_);
}

void rv_editor_terminal::text(uint32_t codepoint, VTermModifier mod)
{
    if (vt_ != nullptr) {
        vterm_keyboard_unichar(vt_, codepoint, mod);
    }
}

void rv_editor_terminal::key(VTermKey key, VTermModifier mod)
{
    if (vt_ != nullptr) {
        vterm_keyboard_key(vt_, key, mod);
    }
}

rv_editor_term_cell rv_editor_terminal::convert(const void *raw) const
{
    VTermScreenCell cell = *static_cast<const VTermScreenCell *>(raw);
    rv_editor_term_cell out;
    // A wide character's second cell holds (uint32_t)-1: no code point, drawn blank.
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0 && cell.chars[i] <= 0x10ffff; ++i) {
        rv_editor_utf8(out.text, cell.chars[i]);
    }
    vterm_screen_convert_color_to_rgb(screen_, &cell.fg);
    vterm_screen_convert_color_to_rgb(screen_, &cell.bg);
    out.fg = (uint32_t(cell.fg.rgb.red) << 16) | (uint32_t(cell.fg.rgb.green) << 8) | cell.fg.rgb.blue;
    out.bg = (uint32_t(cell.bg.rgb.red) << 16) | (uint32_t(cell.bg.rgb.green) << 8) | cell.bg.rgb.blue;
    if (cell.attrs.reverse) {
        std::swap(out.fg, out.bg);
    }
    out.bold = cell.attrs.bold != 0;
    out.underline = cell.attrs.underline != 0;
    return out;
}

rv_editor_term_line rv_editor_terminal::line(int row) const
{
    rv_editor_term_line out;
    if (screen_ == nullptr || row < 0 || row >= rows_) {
        return out;
    }
    out.reserve(static_cast<size_t>(cols_));
    for (int c = 0; c < cols_; ++c) {
        VTermScreenCell cell;
        vterm_screen_get_cell(screen_, VTermPos{ row, c }, &cell);
        out.push_back(convert(&cell));
    }
    return out;
}

} // namespace rv_editor
