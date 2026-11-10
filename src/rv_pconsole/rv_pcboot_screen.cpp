// The boot screen picture: one olive window, laid out the same for the whole boot. Glyphs come from
// rv_pchost_text.hpp; every shape is clipped to the frame, and text is drawn only where it fits its area.
#include "rv_pconsole/rv_pcboot_screen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <string>
#include <string_view>
#include <utility>

#include "pdk/rv_err.h"
#include "pdklib/rv_font/rv_font_data.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_theme/rv_theme_olive.hpp"
#include "rv_pconsole/rv_pchost_text.hpp"
#include "rv_pconsole/rv_pchost_ui.hpp"
#include "rv_pconsole/rv_pctexts.hpp"

namespace rv_3dmppc
{
namespace
{

// Log origin tag, the same as the journal's.
inline constexpr const char *RV_PCBOOT_LOG_TAG = "pcboot";

// Text ids in rv_console_texts.toml.
constexpr std::string_view RV_PCBOOT_TITLE_ID = "boot.title";
constexpr std::string_view RV_PCBOOT_STARTING_ID = "boot.starting";
constexpr std::string_view RV_PCBOOT_STARTED_WARNINGS_ID = "boot.started_warnings";
constexpr std::string_view RV_PCBOOT_STOPPED_ID = "boot.stopped";
constexpr std::string_view RV_PCBOOT_CANNOT_START_ID = "boot.cannot_start";
constexpr std::string_view RV_PCBOOT_MAINTENANCE_ID = "boot.maintenance";
constexpr std::string_view RV_PCBOOT_POWER_OFF_ID = "boot.power_off";
constexpr std::string_view RV_PCBOOT_NO_ID = "";
constexpr std::string_view RV_PCBOOT_LEVEL_OK_ID = "boot.level_ok";
constexpr std::string_view RV_PCBOOT_LEVEL_WARNING_ID = "boot.level_warning";
constexpr std::string_view RV_PCBOOT_LEVEL_ERROR_ID = "boot.level_error";
constexpr std::string_view RV_PCBOOT_SEPARATOR_ID = "boot.separator";
constexpr std::string_view RV_PCBOOT_FOUND_ID = "boot.found";
constexpr std::string_view RV_PCBOOT_RECOMMEND_ID = "boot.recommend";
constexpr std::string_view RV_PCBOOT_CONTINUE_ID = "boot.continue";

// Placeholder of the warning count in boot.started_warnings.
constexpr std::string_view RV_PCBOOT_COUNT_MARK = "{}";

// Theme colours as host words.
constexpr uint32_t RV_PCBOOT_DARK = rv_pchost_ui_argb(rv_pdklib::rv_theme_dark);
constexpr uint32_t RV_PCBOOT_WINDOW = rv_pchost_ui_argb(rv_pdklib::rv_theme_window);
constexpr uint32_t RV_PCBOOT_INSET = rv_pchost_ui_argb(rv_pdklib::rv_theme_inset);
constexpr uint32_t RV_PCBOOT_BUTTON = rv_pchost_ui_argb(rv_pdklib::rv_theme_button);
constexpr uint32_t RV_PCBOOT_TEXT = rv_pchost_ui_argb(rv_pdklib::rv_theme_text);
constexpr uint32_t RV_PCBOOT_TEXT_BRIGHT = rv_pchost_ui_argb(rv_pdklib::rv_theme_text_bright);

// Glyphs are drawn at the font's own size: the 8x8 cell is the screen's unit of text.
constexpr int RV_PCBOOT_TEXT_SCALE = 1;
constexpr int64_t RV_PCBOOT_CELL = rv_pdklib::rv_font_cell_height;
constexpr char RV_PCBOOT_WORD_SEPARATOR = ' '; // Between the words of a wrapped text

// Window: the frame minus the margins, clamped at 0.
constexpr int64_t RV_PCBOOT_MARGIN_X = rv_pdklib::rv_theme_window_margin_x;
constexpr int64_t RV_PCBOOT_MARGIN_Y = rv_pdklib::rv_theme_window_margin_y;
constexpr int64_t RV_PCBOOT_BEVEL = rv_pdklib::rv_theme_bevel_px;

// Title bar: inside the window bevel; the title is bold, the same glyphs drawn a pixel apart.
constexpr int64_t RV_PCBOOT_TITLE_HEIGHT = rv_pdklib::rv_theme_title_h;
constexpr int64_t RV_PCBOOT_TITLE_TEXT_X = rv_pdklib::rv_theme_title_text_x;
constexpr int64_t RV_PCBOOT_TITLE_TEXT_Y = rv_pdklib::rv_theme_title_text_y;
constexpr int64_t RV_PCBOOT_BOLD_SHIFT = 1;

// Status row: icon at the left, text beside it; the text's fit area starts just under the title bar.
constexpr int64_t RV_PCBOOT_ICON_X = 10;
constexpr int64_t RV_PCBOOT_ICON_Y = 24;
constexpr int64_t RV_PCBOOT_ICON_COLUMNS = 8;
constexpr int64_t RV_PCBOOT_ICON_ROWS = 10;
constexpr int64_t RV_PCBOOT_ICON_SCALE = 2;
constexpr int64_t RV_PCBOOT_STATUS_X = 36;
constexpr int64_t RV_PCBOOT_STATUS_Y = 30;
constexpr int64_t RV_PCBOOT_STATUS_TOP = 18;
constexpr int64_t RV_PCBOOT_STATUS_RIGHT = 8; // Status text's right limit, from the window's right edge

// Icons: rows of 8 pixels, MSB leftmost.
enum class icon {
    hourglass,
    note,
    cross,
};
// clang-format off
constexpr uint8_t RV_PCBOOT_ICON_BITS[][RV_PCBOOT_ICON_ROWS] = {
    { 0xFF, 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x5A, 0x7E, 0xFF }, // hourglass
    { 0xFF, 0x81, 0x99, 0x99, 0x99, 0x99, 0x81, 0x99, 0x81, 0xFF }, // note
    { 0xFF, 0x81, 0xC3, 0xA5, 0x99, 0x99, 0xA5, 0xC3, 0x81, 0xFF }, // cross
};
// clang-format on

// Journal panel: a sunken plate under the status row, down to just above the button band.
constexpr int64_t RV_PCBOOT_PANEL_Y = 50;
constexpr int64_t RV_PCBOOT_PANEL_INSET = rv_pdklib::rv_theme_content_x;     // Window edge to the panel, both sides
constexpr int64_t RV_PCBOOT_PANEL_TO_BAND = 6;                               // Panel bottom to the button band's top
constexpr int64_t RV_PCBOOT_PANEL_TEXT_X = rv_pdklib::rv_theme_panel_text_x; // Panel edge to the text, both sides
constexpr int64_t RV_PCBOOT_PANEL_TEXT_Y = 5;
constexpr int64_t RV_PCBOOT_PANEL_TEXT_BOTTOM = 2; // Last line's cell to the panel bottom
constexpr int64_t RV_PCBOOT_LINE_PITCH = rv_pdklib::rv_theme_line_pitch;
constexpr int64_t RV_PCBOOT_NO_INDENT = 0;
constexpr int64_t RV_PCBOOT_FOUND_INDENT = 16; // Found and Recommended lines, from the line area's left

// Band: button or prompt, its top measured up from the window's bottom edge.
constexpr int64_t RV_PCBOOT_BAND_ABOVE_BOTTOM = rv_pdklib::rv_theme_button_band_above_bottom;
constexpr int64_t RV_PCBOOT_BUTTON_PAD_X = rv_pdklib::rv_theme_button_pad_x; // Label to the button's edge
constexpr int64_t RV_PCBOOT_BUTTON_HEIGHT = rv_pdklib::rv_theme_button_h;
constexpr int64_t RV_PCBOOT_BUTTON_TEXT_Y = rv_pdklib::rv_theme_button_text_y;
constexpr int64_t RV_PCBOOT_PROMPT_Y = 4; // Band top to the prompt's cell

// What each state shows. Indexed by rv_pcboot_state; a state without a button has an empty button id.
struct state_look {
    icon icon_kind;
    std::string_view status_id;
    std::string_view button_id;
};
constexpr std::array<state_look, 4> RV_PCBOOT_LOOKS = { {
    { icon::hourglass, RV_PCBOOT_STARTING_ID, RV_PCBOOT_MAINTENANCE_ID },
    { icon::note, RV_PCBOOT_STARTED_WARNINGS_ID, RV_PCBOOT_NO_ID },
    { icon::note, RV_PCBOOT_STOPPED_ID, RV_PCBOOT_NO_ID },
    { icon::cross, RV_PCBOOT_CANNOT_START_ID, RV_PCBOOT_POWER_OFF_ID },
} };

// Journal level words, indexed by rv_pcboot_level.
constexpr std::array<std::string_view, 3> RV_PCBOOT_LEVEL_IDS = {
    RV_PCBOOT_LEVEL_OK_ID,
    RV_PCBOOT_LEVEL_WARNING_ID,
    RV_PCBOOT_LEVEL_ERROR_ID,
};

const state_look &look_of(rv_pcboot_state state)
{
    return RV_PCBOOT_LOOKS[static_cast<std::size_t>(state)];
}

rv_pcboot_rect window_of(int64_t width, int64_t height)
{
    return { RV_PCBOOT_MARGIN_X,
        RV_PCBOOT_MARGIN_Y,
        std::max<int64_t>(0, width - 2 * RV_PCBOOT_MARGIN_X),
        std::max<int64_t>(0, height - 2 * RV_PCBOOT_MARGIN_Y) };
}

int64_t band_top_of(const rv_pcboot_rect &window)
{
    return window.y + window.h - RV_PCBOOT_BAND_ABOVE_BOTTOM;
}

rv_pcboot_rect panel_of(const rv_pcboot_rect &window)
{
    const int64_t y = window.y + RV_PCBOOT_PANEL_Y;
    return { window.x + RV_PCBOOT_PANEL_INSET,
        y,
        window.w - 2 * RV_PCBOOT_PANEL_INSET,
        band_top_of(window) - RV_PCBOOT_PANEL_TO_BAND - y };
}

// The button labelled `button_id`, centred in the band.
rv_pcboot_rect button_of(const rv_pcboot_rect &window, std::string_view button_id)
{
    const int64_t label = rv_pchost_text_ink_width(rv_pctext(button_id), RV_PCBOOT_TEXT_SCALE);
    // The label is dropped when the button is narrower than the label plus its padding on both sides.
    const int64_t natural = label + 2 * RV_PCBOOT_BUTTON_PAD_X;
    const int64_t room = std::max<int64_t>(0, window.w - 2 * RV_PCBOOT_BEVEL);
    const int64_t w = std::min(natural, room);
    // Kept inside the window on a short frame.
    const int64_t y = std::max(band_top_of(window), window.y + RV_PCBOOT_BEVEL);
    return { window.x + (window.w - w) / 2, y, w, RV_PCBOOT_BUTTON_HEIGHT };
}

// Whether the cell at (x, y) and the whole of `text`, plus `extra` pixels of ink, lie inside `area`.
bool text_fits(const rv_pcboot_rect &area, int64_t x, int64_t y, std::string_view text, int64_t extra = 0)
{
    const int64_t ink = rv_pchost_text_ink_width(text, RV_PCBOOT_TEXT_SCALE) + extra;
    return x >= area.x && x + ink <= area.x + area.w && y >= area.y && y + RV_PCBOOT_CELL <= area.y + area.h;
}

// Adds `n` to the unfit counter when the caller asked for one.
void count_unfit(int64_t *unfit, int64_t n = 1)
{
    if (unfit != nullptr) {
        *unfit += n;
    }
}

// `text` at (x, y) when it fits `area`; otherwise it is not drawn and counted once in `unfit`.
void draw_fitting(const rv_pchost_text_target &target,
    const rv_pcboot_rect &area,
    int64_t x,
    int64_t y,
    std::string_view text,
    uint32_t argb,
    int64_t *unfit)
{
    if (!text_fits(area, x, y, text)) {
        count_unfit(unfit);
        return;
    }
    rv_pchost_text_draw(target, x, y, text, RV_PCBOOT_TEXT_SCALE, argb);
}

void draw_icon(const rv_pchost_text_target &target, icon kind, int64_t x, int64_t y, uint32_t argb)
{
    const uint8_t *rows = RV_PCBOOT_ICON_BITS[static_cast<std::size_t>(kind)];
    for (int64_t row = 0; row < RV_PCBOOT_ICON_ROWS; ++row) {
        for (int64_t column = 0; column < RV_PCBOOT_ICON_COLUMNS; ++column) {
            if ((rows[row] & (RV_PCHOST_TEXT_ROW_MSB >> column)) != 0) {
                rv_pchost_text_fill_texel(target,
                    x + column * RV_PCBOOT_ICON_SCALE,
                    y + row * RV_PCBOOT_ICON_SCALE,
                    static_cast<int>(RV_PCBOOT_ICON_SCALE),
                    argb);
            }
        }
    }
}

// "<label><separator><text>", both ids looked up in the console texts.
std::string labelled(std::string_view label_id, std::string_view text_id)
{
    return std::string(rv_pctext(label_id)) + rv_pctext(RV_PCBOOT_SEPARATOR_ID) + rv_pctext(text_id);
}

// The status text of `state`; the warning count replaces the first "{}" when the text has one.
std::string status_text(const rv_pcboot_journal &journal, rv_pcboot_state state)
{
    std::string text = rv_pctext(look_of(state).status_id);
    if (state != rv_pcboot_state::warning) {
        return text;
    }
    const std::size_t mark = text.find(RV_PCBOOT_COUNT_MARK);
    if (mark != std::string::npos) {
        text.replace(mark, RV_PCBOOT_COUNT_MARK.size(), std::to_string(journal.warning_count()));
    }
    return text;
}

// The window plate and its title bar.
void draw_window(const rv_pchost_text_target &target, const rv_pcboot_rect &window, int64_t *unfit)
{
    rv_pchost_ui_raised(target, window, RV_PCBOOT_WINDOW);

    const rv_pcboot_rect bar = { window.x + RV_PCBOOT_BEVEL,
        window.y + RV_PCBOOT_BEVEL,
        window.w - 2 * RV_PCBOOT_BEVEL,
        RV_PCBOOT_TITLE_HEIGHT };
    rv_pchost_ui_fill(target, bar, RV_PCBOOT_DARK);
    const int64_t x = window.x + RV_PCBOOT_TITLE_TEXT_X;
    const int64_t y = window.y + RV_PCBOOT_TITLE_TEXT_Y;
    const std::string_view title = rv_pctext(RV_PCBOOT_TITLE_ID);
    if (!text_fits(bar, x, y, title, RV_PCBOOT_BOLD_SHIFT)) {
        count_unfit(unfit);
        return;
    }
    rv_pchost_text_draw(target, x, y, title, RV_PCBOOT_TEXT_SCALE, RV_PCBOOT_TEXT_BRIGHT);
    rv_pchost_text_draw(target, x + RV_PCBOOT_BOLD_SHIFT, y, title, RV_PCBOOT_TEXT_SCALE, RV_PCBOOT_TEXT_BRIGHT);
}

// The icon and the status text of `state`.
void draw_status(const rv_pchost_text_target &target,
    const rv_pcboot_rect &window,
    const rv_pcboot_rect &panel,
    const rv_pcboot_journal &journal,
    rv_pcboot_state state,
    int64_t *unfit)
{
    draw_icon(target, look_of(state).icon_kind, window.x + RV_PCBOOT_ICON_X, window.y + RV_PCBOOT_ICON_Y, RV_PCBOOT_TEXT);

    const int64_t x = window.x + RV_PCBOOT_STATUS_X;
    const int64_t top = window.y + RV_PCBOOT_STATUS_TOP;
    const rv_pcboot_rect area = { x, top, window.x + window.w - RV_PCBOOT_STATUS_RIGHT - x, panel.y - top };
    draw_fitting(target, area, x, window.y + RV_PCBOOT_STATUS_Y, status_text(journal, state), RV_PCBOOT_TEXT, unfit);
}

// Clears `out`, logs the reason of the failed build `rv` when memory allows the log, and returns `rv`.
int fail_frame(std::vector<uint32_t> &out, int rv, int64_t width, int64_t height)
{
    out.clear();
    try {
        const std::string_view reason = rv == RV_ERR_NOMEM ? "ran out of memory" : "is empty or too large for the pixel buffer";
        RV_LOG_ERR(RV_PCBOOT_LOG_TAG, "boot screen frame {}x{} {}", width, height, reason);
    } catch (const std::bad_alloc &) {
        // The log needs memory too: the reason is lost here, and the returned code still carries it.
    }
    return rv;
}

// Sizes `out` to width * height and fills it with the dark frame colour.
// Zeroes `unfit`, so it is 0 on every return that did not count anything.
int begin_frame(std::vector<uint32_t> &out, int64_t width, int64_t height, int64_t *unfit)
{
    if (unfit != nullptr) {
        *unfit = 0;
    }
    // Checked by division: width * height must fit `out` without wrapping.
    const uint64_t max_pixels = out.max_size();
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) > max_pixels / static_cast<uint64_t>(height)) {
        return fail_frame(out, RV_ERR_INVAL, width, height);
    }
    const auto count = static_cast<std::size_t>(width * height);
    try {
        out.assign(count, RV_PCBOOT_DARK);
    } catch (const std::bad_alloc &) {
        return fail_frame(out, RV_ERR_NOMEM, width, height);
    }
    return RV_OK;
}

// One line of the journal panel, drawn `indent` pixels from the line area's left.
struct journal_line {
    int64_t indent;
    std::string text;
};

// Word-wraps `text` to `width` pixels onto `lines`, every line at `indent`. A text with
// a word wider than `width` is not drawn at all: nothing is added, and it counts once in `unfit`.
void wrap_into(std::vector<journal_line> &lines, std::string_view text, int64_t width, int64_t indent, int64_t *unfit)
{
    const std::size_t start = lines.size();
    std::string current;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t end = std::min(text.find(RV_PCBOOT_WORD_SEPARATOR, pos), text.size());
        const std::string_view word = text.substr(pos, end - pos);
        pos = end + 1;
        if (word.empty()) {
            continue;
        }
        if (rv_pchost_text_ink_width(word, RV_PCBOOT_TEXT_SCALE) > width) {
            lines.resize(start);
            count_unfit(unfit);
            return;
        }
        std::string candidate = current;
        if (!current.empty()) {
            candidate += RV_PCBOOT_WORD_SEPARATOR;
        }
        candidate += word;
        if (rv_pchost_text_ink_width(candidate, RV_PCBOOT_TEXT_SCALE) <= width) {
            current = std::move(candidate);
            continue;
        }
        lines.push_back({ indent, std::move(current) });
        current = std::string(word);
    }
    if (!current.empty()) {
        lines.push_back({ indent, std::move(current) });
    }
}

// The journal's lines in order: each entry's header, and under each warning or error its short
// Found and Recommended texts (the journal writes the full ones to the log).
// `width` is the line area's width; `unfit` counts the texts that wrap_into drops.
std::vector<journal_line> journal_lines(const rv_pcboot_journal &journal, int64_t width, int64_t *unfit)
{
    std::vector<journal_line> lines;
    const int64_t indented = width - RV_PCBOOT_FOUND_INDENT;
    for (const rv_pcboot_entry &entry : journal.entries()) {
        // The header is one line, not wrapped: the journal stops at it when it does not fit.
        const char *level = rv_pctext(RV_PCBOOT_LEVEL_IDS[static_cast<std::size_t>(entry.level)]);
        lines.push_back({ RV_PCBOOT_NO_INDENT, entry.name + rv_pctext(RV_PCBOOT_SEPARATOR_ID) + level });
        if (entry.level == rv_pcboot_level::ok) {
            continue;
        }
        wrap_into(lines, labelled(RV_PCBOOT_FOUND_ID, entry.screen_found_id), indented, RV_PCBOOT_FOUND_INDENT, unfit);
        wrap_into(lines, labelled(RV_PCBOOT_RECOMMEND_ID, entry.screen_recommend_id), indented, RV_PCBOOT_FOUND_INDENT, unfit);
    }
    return lines;
}

// The sunken panel with the journal's lines; the first line that does not fit ends the list,
// and every line from there on counts as unfit.
void draw_journal(const rv_pchost_text_target &target,
    const rv_pcboot_rect &panel,
    const rv_pcboot_journal &journal,
    int64_t *unfit)
{
    rv_pchost_ui_sunken(target, panel, RV_PCBOOT_INSET);

    const int64_t x = panel.x + RV_PCBOOT_PANEL_TEXT_X;
    const int64_t y = panel.y + RV_PCBOOT_PANEL_TEXT_Y;
    const rv_pcboot_rect area = { x,
        y,
        panel.x + panel.w - RV_PCBOOT_PANEL_TEXT_X - x,
        panel.y + panel.h - RV_PCBOOT_PANEL_TEXT_BOTTOM - y };
    const std::vector<journal_line> lines = journal_lines(journal, area.w, unfit);
    int64_t line_y = y;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int64_t line_x = x + lines[i].indent;
        if (!text_fits(area, line_x, line_y, lines[i].text)) {
            count_unfit(unfit, static_cast<int64_t>(lines.size() - i));
            return;
        }
        rv_pchost_text_draw(target, line_x, line_y, lines[i].text, RV_PCBOOT_TEXT_SCALE, RV_PCBOOT_TEXT);
        line_y += RV_PCBOOT_LINE_PITCH;
    }
}

// The button band: the state's button, or for a state without one the continue prompt.
void draw_band(const rv_pchost_text_target &target, const rv_pcboot_rect &window, rv_pcboot_state state, int64_t *unfit)
{
    const std::string_view button_id = look_of(state).button_id;
    if (button_id.empty()) {
        const std::string_view prompt = rv_pctext(RV_PCBOOT_CONTINUE_ID);
        const int64_t top = band_top_of(window);
        const rv_pcboot_rect area = { window.x + RV_PCBOOT_BEVEL,
            top,
            window.w - 2 * RV_PCBOOT_BEVEL,
            window.y + window.h - RV_PCBOOT_BEVEL - top };
        const int64_t x = window.x + (window.w - rv_pchost_text_ink_width(prompt, RV_PCBOOT_TEXT_SCALE)) / 2;
        draw_fitting(target, area, x, top + RV_PCBOOT_PROMPT_Y, prompt, RV_PCBOOT_TEXT, unfit);
        return;
    }

    const rv_pcboot_rect button = button_of(window, button_id);
    rv_pchost_ui_raised(target, button, RV_PCBOOT_BUTTON);
    const rv_pcboot_rect label_area = { button.x + RV_PCBOOT_BUTTON_PAD_X,
        button.y,
        button.w - 2 * RV_PCBOOT_BUTTON_PAD_X,
        button.h };
    const std::string_view label = rv_pctext(button_id);
    draw_fitting(target, label_area, label_area.x, button.y + RV_PCBOOT_BUTTON_TEXT_Y, label, RV_PCBOOT_TEXT, unfit);
}

// The whole picture on `target`, back to front.
void draw_screen(const rv_pchost_text_target &target, const rv_pcboot_journal &journal, rv_pcboot_state state, int64_t *unfit)
{
    const rv_pcboot_rect window = window_of(target.width, target.height);
    const rv_pcboot_rect panel = panel_of(window);
    draw_window(target, window, unfit);
    draw_status(target, window, panel, journal, state, unfit);
    draw_journal(target, panel, journal, unfit);
    draw_band(target, window, state, unfit);
}

} // namespace

rv_pcboot_rect rv_pcboot_button_rect(int64_t width, int64_t height, rv_pcboot_state state)
{
    const std::string_view button_id = look_of(state).button_id;
    if (button_id.empty()) {
        return { 0, 0, 0, 0 };
    }
    return button_of(window_of(width, height), button_id);
}

int rv_pcboot_screen_build(std::vector<uint32_t> &out,
    int64_t width,
    int64_t height,
    const rv_pcboot_journal &journal,
    rv_pcboot_state state,
    int64_t *unfit)
{
    const int rv = begin_frame(out, width, height, unfit);
    if (rv != RV_OK) {
        return rv;
    }
    // The line lists and strings may throw bad_alloc after `out` is sized; that is RV_ERR_NOMEM too.
    try {
        draw_screen(rv_pchost_text_target{ out.data(), width, height }, journal, state, unfit);
    } catch (const std::bad_alloc &) {
        return fail_frame(out, RV_ERR_NOMEM, width, height);
    }
    return RV_OK;
}

} // namespace rv_3dmppc
