// The boot screen pictures. Glyphs come from rv_pchost_text.hpp; every shape is
// clipped to the frame, and text is drawn only where it fits its area.
#include "rv_pconsole/rv_pcboot_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <utility>

#include "pdk/rv_err.h"
#include "pdklib/rv_font/rv_font_data.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pchost_text.hpp"
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
constexpr std::string_view RV_PCBOOT_BUTTON_ID = "boot.maintenance";
constexpr std::string_view RV_PCBOOT_LEVEL_OK_ID = "boot.level_ok";
constexpr std::string_view RV_PCBOOT_LEVEL_WARNING_ID = "boot.level_warning";
constexpr std::string_view RV_PCBOOT_SEPARATOR_ID = "boot.separator";
constexpr std::string_view RV_PCBOOT_ALL_OK_ID = "boot.all_ok";
constexpr std::string_view RV_PCBOOT_FOUND_ID = "boot.found";
constexpr std::string_view RV_PCBOOT_RECOMMEND_ID = "boot.recommend";
constexpr std::string_view RV_PCBOOT_CONTINUE_ID = "boot.continue";

// Colours, ARGB. Grey window and bevels in the workstation style; the dark desk
// makes the window read as a window.
constexpr uint32_t RV_PCBOOT_DESK = 0xFF2F3E4Eu;      // Backdrop around the window
constexpr uint32_t RV_PCBOOT_BLACK = 0xFF000000u;     // Outline and all ink
constexpr uint32_t RV_PCBOOT_WHITE = 0xFFFFFFFFu;     // Title text and lit bevel edge
constexpr uint32_t RV_PCBOOT_GREY = 0xFFC0C0C0u;      // Window body and button face
constexpr uint32_t RV_PCBOOT_SHADE = 0xFF808080u;     // Shaded bevel edge
constexpr uint32_t RV_PCBOOT_TITLE_BAR = 0xFF3C4A5Cu; // Behind the title text

// Glyphs are drawn at the font's own size: the 8x8 cell is the screen's unit of text.
constexpr int RV_PCBOOT_TEXT_SCALE = 1;
constexpr int64_t RV_PCBOOT_CELL = rv_pdklib::rv_font_cell_height;

// Window placement: the desk shows around the window, and everything inside is
// inset from the window edge.
constexpr int64_t RV_PCBOOT_MARGIN_X = 32;     // Desk left and right of the window
constexpr int64_t RV_PCBOOT_MARGIN_Y = 24;     // Desk above and below the window
constexpr int64_t RV_PCBOOT_BORDER = 3;        // Window edge to title bar and body
constexpr int64_t RV_PCBOOT_TITLE_HEIGHT = 14; // One cell and room round it
constexpr int64_t RV_PCBOOT_TITLE_TEXT_X = 4;  // Title text inset from the bar's left
constexpr int64_t RV_PCBOOT_TITLE_TEXT_Y = 3;  // Centres the cell in the bar
constexpr int64_t RV_PCBOOT_BODY_GAP = 6;      // Title bar to the body

// Hourglass: rows of 8 pixels, MSB leftmost, drawn at twice its size so it reads
// at the font's scale.
// clang-format off
constexpr uint8_t RV_PCBOOT_GLASS_ROWS[] = {
    0xFF, 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x5A, 0x7E, 0xFF,
};
// clang-format on
constexpr int64_t RV_PCBOOT_GLASS_COLUMNS = 8;
constexpr int64_t RV_PCBOOT_GLASS_SCALE = 2;
constexpr int64_t RV_PCBOOT_GLASS_HEIGHT = static_cast<int64_t>(std::size(RV_PCBOOT_GLASS_ROWS)) * RV_PCBOOT_GLASS_SCALE;
constexpr int64_t RV_PCBOOT_GLASS_X = 12;   // Window edge to the hourglass
constexpr int64_t RV_PCBOOT_GLASS_GAP = 10; // Hourglass to the starting line

// Journal: one line per entry, below the hourglass.
constexpr int64_t RV_PCBOOT_LINES_GAP = 12; // Hourglass bottom to the first line
constexpr int64_t RV_PCBOOT_LINE_PITCH = RV_PCBOOT_CELL;
constexpr int64_t RV_PCBOOT_LINES_TO_BUTTON = 8; // Last line to the button

// Report: text starts inside the window edge; Found and Recommended are indented
// from that, and the last line keeps a gap above the prompt.
constexpr int64_t RV_PCBOOT_REPORT_X = 12;      // Window edge to the report text
constexpr int64_t RV_PCBOOT_REPORT_INDENT = 16; // Report text to the Found and Recommended lines
constexpr int64_t RV_PCBOOT_REPORT_GAP = 8;     // Last report line to the prompt
constexpr int64_t RV_PCBOOT_NO_INDENT = 0;      // The all-ok line and the entry lines start at the text left
constexpr char RV_PCBOOT_WORD_SEPARATOR = ' ';  // Between the words of a wrapped text

// Button: a raised face at the bottom of the window, centred. The report's prompt
// is centred in the same band.
constexpr int64_t RV_PCBOOT_BUTTON_PAD_X = 8;   // Label to the button's edge
constexpr int64_t RV_PCBOOT_BUTTON_HEIGHT = 16; // Cell plus padding above and below
constexpr int64_t RV_PCBOOT_BUTTON_BOTTOM = 12; // Button bottom to the window bottom

// A rectangle shrunk by `by` on every side.
rv_pcboot_rect inset(const rv_pcboot_rect &rect, int64_t by)
{
    return { rect.x + by, rect.y + by, rect.w - 2 * by, rect.h - 2 * by };
}

void fill_rect(const rv_pchost_text_target &target, const rv_pcboot_rect &rect, uint32_t argb)
{
    const int64_t x0 = std::max<int64_t>(rect.x, 0);
    const int64_t y0 = std::max<int64_t>(rect.y, 0);
    const int64_t x1 = std::min<int64_t>(rect.x + rect.w, target.width);
    const int64_t y1 = std::min<int64_t>(rect.y + rect.h, target.height);
    for (int64_t y = y0; y < y1; ++y) {
        for (int64_t x = x0; x < x1; ++x) {
            target.pixels[y * target.width + x] = argb;
        }
    }
}

// One-pixel raised edge: light on the top and left, shade on the bottom and right.
void bevel(const rv_pchost_text_target &target, const rv_pcboot_rect &rect)
{
    // An empty rect would paint one pixel outside it.
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }
    fill_rect(target, { rect.x, rect.y, rect.w, 1 }, RV_PCBOOT_WHITE);
    fill_rect(target, { rect.x, rect.y, 1, rect.h }, RV_PCBOOT_WHITE);
    fill_rect(target, { rect.x, rect.y + rect.h - 1, rect.w, 1 }, RV_PCBOOT_SHADE);
    fill_rect(target, { rect.x + rect.w - 1, rect.y, 1, rect.h }, RV_PCBOOT_SHADE);
}

// Whether the cell at (x, y) and the whole of `text` lie inside `area`.
bool text_fits(const rv_pcboot_rect &area, int64_t x, int64_t y, std::string_view text)
{
    const int64_t ink = rv_pchost_text_ink_width(text, RV_PCBOOT_TEXT_SCALE);
    return x >= area.x && x + ink <= area.x + area.w && y >= area.y && y + RV_PCBOOT_CELL <= area.y + area.h;
}

// `text` with its cell's top left at (x, y); the caller has checked text_fits.
void draw_text(const rv_pchost_text_target &target, int64_t x, int64_t y, std::string_view text, uint32_t argb)
{
    rv_pchost_text_draw(target, x, y, text, RV_PCBOOT_TEXT_SCALE, argb);
}

void draw_hourglass(const rv_pchost_text_target &target, int64_t x, int64_t y)
{
    for (std::size_t row = 0; row < std::size(RV_PCBOOT_GLASS_ROWS); ++row) {
        for (int64_t column = 0; column < RV_PCBOOT_GLASS_COLUMNS; ++column) {
            if ((RV_PCBOOT_GLASS_ROWS[row] & (RV_PCHOST_TEXT_ROW_MSB >> column)) != 0) {
                rv_pchost_text_fill_texel(target,
                    x + column * RV_PCBOOT_GLASS_SCALE,
                    y + static_cast<int64_t>(row) * RV_PCBOOT_GLASS_SCALE,
                    static_cast<int>(RV_PCBOOT_GLASS_SCALE),
                    RV_PCBOOT_BLACK);
            }
        }
    }
}

rv_pcboot_rect window_of(int64_t width, int64_t height)
{
    return { RV_PCBOOT_MARGIN_X,
        RV_PCBOOT_MARGIN_Y,
        std::max<int64_t>(0, width - 2 * RV_PCBOOT_MARGIN_X),
        std::max<int64_t>(0, height - 2 * RV_PCBOOT_MARGIN_Y) };
}

// The title bar: the top band of the window, inside its border.
rv_pcboot_rect title_of(const rv_pcboot_rect &window)
{
    return { window.x + RV_PCBOOT_BORDER,
        window.y + RV_PCBOOT_BORDER,
        window.w - 2 * RV_PCBOOT_BORDER,
        RV_PCBOOT_TITLE_HEIGHT };
}

// The window below the title bar: where both views put their text.
rv_pcboot_rect body_of(const rv_pcboot_rect &window)
{
    const int64_t top = title_of(window).y + RV_PCBOOT_TITLE_HEIGHT + RV_PCBOOT_BODY_GAP;
    return { window.x + RV_PCBOOT_BORDER, top, window.w - 2 * RV_PCBOOT_BORDER, window.y + window.h - RV_PCBOOT_BORDER - top };
}

// The journal's level word, from the console texts.
std::string_view level_id(rv_pcboot_level level)
{
    return level == rv_pcboot_level::ok ? RV_PCBOOT_LEVEL_OK_ID : RV_PCBOOT_LEVEL_WARNING_ID;
}

// "<label><separator><text>", both ids looked up in the console texts.
std::string labelled(std::string_view label_id, std::string_view text_id)
{
    return std::string(rv_pctext(label_id)) + rv_pctext(RV_PCBOOT_SEPARATOR_ID) + rv_pctext(text_id);
}

// The grey window with its title bar, shared by both views.
void draw_window(const rv_pchost_text_target &target, const rv_pcboot_rect &window)
{
    // Black outline, grey body, raised edge.
    fill_rect(target, window, RV_PCBOOT_BLACK);
    fill_rect(target, inset(window, 1), RV_PCBOOT_GREY);
    bevel(target, inset(window, 1));

    // Title bar with the product name at its left.
    const rv_pcboot_rect title = title_of(window);
    fill_rect(target, title, RV_PCBOOT_TITLE_BAR);
    const int64_t title_x = title.x + RV_PCBOOT_TITLE_TEXT_X;
    const int64_t title_y = title.y + RV_PCBOOT_TITLE_TEXT_Y;
    const std::string_view title_text = rv_pctext(RV_PCBOOT_TITLE_ID);
    if (text_fits(title, title_x, title_y, title_text)) {
        draw_text(target, title_x, title_y, title_text, RV_PCBOOT_WHITE);
    }
}

// Clears `out`, logs the reason of the failed build `rv` and returns `rv`.
int refuse(std::vector<uint32_t> &out, int rv, int64_t width, int64_t height)
{
    out.clear();
    try {
        if (rv == RV_ERR_NOMEM) {
            RV_LOG_ERR(RV_PCBOOT_LOG_TAG, "boot screen frame {}x{} ran out of memory", width, height);
        } else {
            RV_LOG_ERR(RV_PCBOOT_LOG_TAG, "boot screen frame {}x{} is empty or too large for the pixel buffer", width, height);
        }
    } catch (const std::bad_alloc &) {
        // The log needs memory too: the reason is lost here, and the returned code still carries it.
    }
    return rv;
}

// Sizes `out` to width * height and fills it with the desk colour, both views' first step.
int begin_frame(std::vector<uint32_t> &out, int64_t width, int64_t height)
{
    // Checked by division: width * height must fit `out` without wrapping.
    const uint64_t max_pixels = out.max_size();
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) > max_pixels / static_cast<uint64_t>(height)) {
        return refuse(out, RV_ERR_INVAL, width, height);
    }
    const auto count = static_cast<std::size_t>(width * height);
    try {
        out.assign(count, RV_PCBOOT_DESK);
    } catch (const std::bad_alloc &) {
        return refuse(out, RV_ERR_NOMEM, width, height);
    }
    return RV_OK;
}

// One line of a report text, drawn `indent` pixels from the report's text left.
struct report_line {
    int64_t indent;
    std::string text;
};

// Word-wraps `text` to `width` pixels onto `lines`, every line at `indent`. A text with
// a word wider than `width` is not drawn at all: nothing is added.
void wrap_into(std::vector<report_line> &lines, std::string_view text, int64_t width, int64_t indent)
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

// The report's lines in order: the all-ok line when nothing failed, then each entry,
// and under each warning its short Found and Recommended texts (the journal writes the full ones to the log).
// `width` is the text area.
std::vector<report_line> report_lines(const rv_pcboot_journal &journal, int64_t width)
{
    std::vector<report_line> lines;
    if (!journal.has_warning()) {
        wrap_into(lines, rv_pctext(RV_PCBOOT_ALL_OK_ID), width, RV_PCBOOT_NO_INDENT);
    }
    const int64_t indented = width - RV_PCBOOT_REPORT_INDENT;
    for (const rv_pcboot_entry &entry : journal.entries()) {
        // The header is one line, not wrapped: the journal stops at it when it does not fit.
        lines.push_back(
            { RV_PCBOOT_NO_INDENT, entry.name + rv_pctext(RV_PCBOOT_SEPARATOR_ID) + rv_pctext(level_id(entry.level)) });
        if (entry.level != rv_pcboot_level::warning) {
            continue;
        }
        wrap_into(lines, labelled(RV_PCBOOT_FOUND_ID, entry.screen_found_id), indented, RV_PCBOOT_REPORT_INDENT);
        wrap_into(lines, labelled(RV_PCBOOT_RECOMMEND_ID, entry.screen_recommend_id), indented, RV_PCBOOT_REPORT_INDENT);
    }
    return lines;
}

// The starting view on `target`: the window, the hourglass, the starting line, the journal and the button.
void draw_starting(const rv_pchost_text_target &target, const rv_pcboot_journal &journal)
{
    const rv_pcboot_rect window = window_of(target.width, target.height);
    draw_window(target, window);

    // Hourglass, then the starting line beside it.
    const rv_pcboot_rect body = body_of(window);
    const int64_t glass_x = window.x + RV_PCBOOT_GLASS_X;
    draw_hourglass(target, glass_x, body.y);
    const int64_t text_x = glass_x + RV_PCBOOT_GLASS_COLUMNS * RV_PCBOOT_GLASS_SCALE + RV_PCBOOT_GLASS_GAP;
    const int64_t starting_y = body.y + (RV_PCBOOT_GLASS_HEIGHT - RV_PCBOOT_CELL) / 2;
    const std::string_view starting = rv_pctext(RV_PCBOOT_STARTING_ID);
    if (text_fits(body, text_x, starting_y, starting)) {
        draw_text(target, text_x, starting_y, starting, RV_PCBOOT_BLACK);
    }

    // Journal lines in entry order; the journal stops at the first line that does not fit.
    const rv_pcboot_rect button = rv_pcboot_button_rect(target.width, target.height);
    const int64_t lines_top = body.y + RV_PCBOOT_GLASS_HEIGHT + RV_PCBOOT_LINES_GAP;
    const rv_pcboot_rect lines_area = { text_x,
        lines_top,
        window.x + window.w - RV_PCBOOT_BORDER - text_x,
        button.y - RV_PCBOOT_LINES_TO_BUTTON - lines_top };
    int64_t line_y = lines_top;
    for (const rv_pcboot_entry &entry : journal.entries()) {
        const std::string line = entry.name + rv_pctext(RV_PCBOOT_SEPARATOR_ID) + rv_pctext(level_id(entry.level));
        if (!text_fits(lines_area, text_x, line_y, line)) {
            break;
        }
        draw_text(target, text_x, line_y, line, RV_PCBOOT_BLACK);
        line_y += RV_PCBOOT_LINE_PITCH;
    }

    // Button last, so on a tiny frame it stays whole over anything it overlaps.
    fill_rect(target, button, RV_PCBOOT_GREY);
    bevel(target, button);
    bevel(target, inset(button, 1));
    const rv_pcboot_rect label_area = { button.x + RV_PCBOOT_BUTTON_PAD_X,
        button.y,
        button.w - 2 * RV_PCBOOT_BUTTON_PAD_X,
        button.h };
    const std::string_view label = rv_pctext(RV_PCBOOT_BUTTON_ID);
    const int64_t label_x = button.x + (button.w - rv_pchost_text_ink_width(label, RV_PCBOOT_TEXT_SCALE)) / 2;
    const int64_t label_y = button.y + (button.h - RV_PCBOOT_CELL) / 2;
    if (text_fits(label_area, label_x, label_y, label)) {
        draw_text(target, label_x, label_y, label, RV_PCBOOT_BLACK);
    }
}

// The report view on `target`: the lines, then the continue prompt.
void draw_report(const rv_pchost_text_target &target, const rv_pcboot_journal &journal)
{
    const rv_pcboot_rect window = window_of(target.width, target.height);
    draw_window(target, window);

    // Report lines from the body's top; the first line that does not fit ends the list.
    const rv_pcboot_rect body = body_of(window);
    const rv_pcboot_rect button = rv_pcboot_button_rect(target.width, target.height);
    const int64_t text_x = window.x + RV_PCBOOT_REPORT_X;
    const int64_t prompt_y = button.y + (button.h - RV_PCBOOT_CELL) / 2;
    const rv_pcboot_rect lines_area = { text_x, body.y, body.x + body.w - text_x, prompt_y - RV_PCBOOT_REPORT_GAP - body.y };
    int64_t line_y = body.y;
    for (const report_line &line : report_lines(journal, lines_area.w)) {
        const int64_t x = text_x + line.indent;
        if (!text_fits(lines_area, x, line_y, line.text)) {
            break;
        }
        draw_text(target, x, line_y, line.text, RV_PCBOOT_BLACK);
        line_y += RV_PCBOOT_LINE_PITCH;
    }

    // Continue prompt centred on the window, in the button's band.
    const std::string_view prompt = rv_pctext(RV_PCBOOT_CONTINUE_ID);
    const int64_t prompt_x = window.x + (window.w - rv_pchost_text_ink_width(prompt, RV_PCBOOT_TEXT_SCALE)) / 2;
    if (text_fits(body, prompt_x, prompt_y, prompt)) {
        draw_text(target, prompt_x, prompt_y, prompt, RV_PCBOOT_BLACK);
    }
}

} // namespace

rv_pcboot_rect rv_pcboot_button_rect(int64_t width, int64_t height)
{
    const rv_pcboot_rect window = window_of(width, height);
    const int64_t label = rv_pchost_text_ink_width(rv_pctext(RV_PCBOOT_BUTTON_ID), RV_PCBOOT_TEXT_SCALE);
    // The label is dropped when the button is narrower than the label plus its padding on both sides.
    const int64_t natural = label + 2 * RV_PCBOOT_BUTTON_PAD_X;
    const int64_t room = std::max<int64_t>(0, window.w - 2 * RV_PCBOOT_BORDER);
    const int64_t w = std::min(natural, room);
    return { window.x + (window.w - w) / 2,
        window.y + window.h - RV_PCBOOT_BUTTON_BOTTOM - RV_PCBOOT_BUTTON_HEIGHT,
        w,
        RV_PCBOOT_BUTTON_HEIGHT };
}

int rv_pcboot_screen_build(std::vector<uint32_t> &out, int64_t width, int64_t height, const rv_pcboot_journal &journal)
{
    const int rv = begin_frame(out, width, height);
    if (rv != RV_OK) {
        return rv;
    }
    // The line strings may throw bad_alloc after `out` is sized; that is RV_ERR_NOMEM too.
    try {
        draw_starting(rv_pchost_text_target{ out.data(), width, height }, journal);
    } catch (const std::bad_alloc &) {
        return refuse(out, RV_ERR_NOMEM, width, height);
    }
    return RV_OK;
}

int rv_pcboot_report_build(std::vector<uint32_t> &out, int64_t width, int64_t height, const rv_pcboot_journal &journal)
{
    const int rv = begin_frame(out, width, height);
    if (rv != RV_OK) {
        return rv;
    }
    // The line lists and strings may throw bad_alloc after `out` is sized; that is RV_ERR_NOMEM too.
    try {
        draw_report(rv_pchost_text_target{ out.data(), width, height }, journal);
    } catch (const std::bad_alloc &) {
        return refuse(out, RV_ERR_NOMEM, width, height);
    }
    return RV_OK;
}

} // namespace rv_3dmppc
