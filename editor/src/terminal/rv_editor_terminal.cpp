// The Terminal tile's model: libvterm between the PTY and the drawing.

#include "terminal/rv_editor_terminal.hpp"

#include <algorithm>
#include <cstdlib>

#include <vterm.h>

#include "pdk/rv_err.h"
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

// Minimum columns or rows for terminal grid.
constexpr int min_terminal_size = 2;
// ANSI color palette size (standard 16 colours).
constexpr int ansi_palette_size = 16;
// Maximum valid Unicode codepoint.
constexpr uint32_t unicode_max_codepoint = 0x10ffffu;
// PTY read buffer size in bytes (1 MiB).
constexpr size_t pty_read_buffer_size = 1 << 20;
// Environment variable name for user shell.
constexpr std::string_view env_shell_name = "SHELL";
// Fallback shell program.
constexpr std::string_view default_shell_path = "/bin/sh";
// Root filesystem path indicator for absolute path check.
constexpr char path_root_indicator = '/';
// UTF-8 encoding (RFC 3629).
constexpr uint32_t utf8_1byte_limit = 0x80;
constexpr uint32_t utf8_2byte_limit = 0x800;
constexpr uint32_t utf8_3byte_limit = 0x10000;
constexpr uint8_t utf8_lead_2 = 0xc0;
constexpr uint8_t utf8_lead_3 = 0xe0;
constexpr uint8_t utf8_lead_4 = 0xf0;
constexpr uint8_t utf8_cont_byte = 0x80;
constexpr uint8_t utf8_cont_mask = 0x3f;
constexpr int utf8_cont_bits = 6;
constexpr int utf8_shift_2bits = 2 * utf8_cont_bits;
constexpr int utf8_shift_3bits = 3 * utf8_cont_bits;

VTermColor rv_editor_vterm_rgb(uint32_t rgb)
{
    VTermColor c;
    vterm_color_rgb(&c,
        (rgb >> channel_shift_red) & rgb_channel_mask,
        (rgb >> channel_shift_green) & rgb_channel_mask,
        rgb & rgb_channel_mask);
    return c;
}

void rv_editor_utf8(std::string &out, uint32_t cp)
{
    if (cp < utf8_1byte_limit) {
        out += static_cast<char>(cp);
    } else if (cp < utf8_2byte_limit) {
        out += static_cast<char>(utf8_lead_2 | (cp >> utf8_cont_bits));
        out += static_cast<char>(utf8_cont_byte | (cp & utf8_cont_mask));
    } else if (cp < utf8_3byte_limit) {
        out += static_cast<char>(utf8_lead_3 | (cp >> utf8_shift_2bits));
        out += static_cast<char>(utf8_cont_byte | ((cp >> utf8_cont_bits) & utf8_cont_mask));
        out += static_cast<char>(utf8_cont_byte | (cp & utf8_cont_mask));
    } else {
        out += static_cast<char>(utf8_lead_4 | (cp >> utf8_shift_3bits));
        out += static_cast<char>(utf8_cont_byte | ((cp >> utf8_shift_2bits) & utf8_cont_mask));
        out += static_cast<char>(utf8_cont_byte | ((cp >> utf8_cont_bits) & utf8_cont_mask));
        out += static_cast<char>(utf8_cont_byte | (cp & utf8_cont_mask));
    }
}

} // namespace

struct rv_editor_terminal::hooks {
    // What the emulator answers the shell (cursor reports, key sequences).
    static void output(const char *s, size_t len, void *user)
    {
        (void)static_cast<rv_editor_terminal *>(user)->pty_.write(std::string_view(s, len));
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

int rv_editor_terminal::ensure_vt_(const rv_editor_theme &theme, std::string &error)
{
    if (vt_ != nullptr) {
        vterm_set_size(vt_, rows_, cols_);
        return RV_OK;
    }
    vt_ = vterm_new(rows_, cols_);
    if (vt_ == nullptr) {
        error = "cannot create the terminal emulator";
        return RV_ERR_NOMEM;
    }
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
    // The ANSI colours from the code area's Catppuccin Mocha.
    const uint32_t palette[ansi_palette_size] = {
        theme.code_surface,
        theme.code_red,
        theme.code_green,
        theme.code_yellow,
        theme.code_blue,
        theme.code_magenta,
        theme.code_cyan,
        theme.code_subtext,
        theme.code_surface,
        theme.code_red,
        theme.code_green,
        theme.code_yellow,
        theme.code_blue,
        theme.code_magenta,
        theme.code_cyan,
        theme.code_text,
    };
    VTermState *state = vterm_obtain_state(vt_);
    for (int i = 0; i < ansi_palette_size; ++i) {
        const VTermColor c = rv_editor_vterm_rgb(palette[i]);
        vterm_state_set_palette_color(state, i, &c);
    }
    const VTermColor fg = rv_editor_vterm_rgb(theme.code_text);
    const VTermColor bg = rv_editor_vterm_rgb(theme.code_base);
    vterm_screen_set_default_colors(screen_, &fg, &bg);
    vterm_screen_reset(screen_, 1);

    return RV_OK;
}

rv_editor_terminal::~rv_editor_terminal()
{
    pty_.stop();
    if (vt_ != nullptr) {
        vterm_free(vt_);
    }
}

int rv_editor_terminal::start(const std::filesystem::path &cwd,
    int cols,
    int rows,
    const rv_editor_theme &theme,
    std::string &error)
{
    cols_ = std::max(cols, min_terminal_size);
    rows_ = std::max(rows, min_terminal_size);
    const int vt_rc = ensure_vt_(theme, error);
    if (vt_rc != RV_OK) {
        return vt_rc;
    }

    const char *shell = std::getenv(env_shell_name.data());
    const std::string program = shell != nullptr && shell[0] == path_root_indicator ? shell : std::string(default_shell_path);
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
    pty_.read(bytes, pty_read_buffer_size);
    if (!bytes.empty()) {
        vterm_input_write(vt_, bytes.data(), bytes.size());
        vterm_screen_flush_damage(screen_);
    }
    pty_.flush();
    pty_.poll();
}

void rv_editor_terminal::resize(int cols, int rows)
{
    cols = std::max(cols, min_terminal_size);
    rows = std::max(rows, min_terminal_size);
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
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0 && cell.chars[i] <= unicode_max_codepoint; ++i) {
        rv_editor_utf8(out.text, cell.chars[i]);
    }
    vterm_screen_convert_color_to_rgb(screen_, &cell.fg);
    vterm_screen_convert_color_to_rgb(screen_, &cell.bg);
    out.fg = (uint32_t(cell.fg.rgb.red) << channel_shift_red) | (uint32_t(cell.fg.rgb.green) << channel_shift_green) |
        cell.fg.rgb.blue;
    out.bg = (uint32_t(cell.bg.rgb.red) << channel_shift_red) | (uint32_t(cell.bg.rgb.green) << channel_shift_green) |
        cell.bg.rgb.blue;
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
