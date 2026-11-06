// The boot screen picture. Glyphs come from rv_pchost_text.hpp; every shape is
// clipped to the frame, and text is drawn only where it fits its area.
#include "rv_pconsole/rv_pcboot_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <new>
#include <string>
#include <string_view>

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

// Button: a raised face at the bottom of the window, centred.
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

// The journal's level word, from the console texts.
std::string_view level_id(rv_pcboot_level level)
{
    return level == rv_pcboot_level::ok ? RV_PCBOOT_LEVEL_OK_ID : RV_PCBOOT_LEVEL_WARNING_ID;
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
    // Checked by division: width * height must fit `out` without wrapping.
    const uint64_t max_pixels = out.max_size();
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) > max_pixels / static_cast<uint64_t>(height)) {
        out.clear();
        RV_LOG_ERR(RV_PCBOOT_LOG_TAG, "boot screen frame {}x{} is empty or too large for the pixel buffer", width, height);
        return RV_ERR_INVAL;
    }
    const auto count = static_cast<std::size_t>(width * height);
    try {
        out.assign(count, RV_PCBOOT_DESK);
    } catch (const std::bad_alloc &) {
        out.clear();
        RV_LOG_ERR(RV_PCBOOT_LOG_TAG, "boot screen frame {}x{} ({} pixels) does not fit in memory", width, height, count);
        return RV_ERR_NOMEM;
    }
    const rv_pchost_text_target target{ out.data(), width, height };

    // Window: black outline, grey body, raised edge.
    const rv_pcboot_rect window = window_of(width, height);
    fill_rect(target, window, RV_PCBOOT_BLACK);
    fill_rect(target, inset(window, 1), RV_PCBOOT_GREY);
    bevel(target, inset(window, 1));

    // Title bar with the product name at its left.
    const rv_pcboot_rect title = { window.x + RV_PCBOOT_BORDER,
        window.y + RV_PCBOOT_BORDER,
        window.w - 2 * RV_PCBOOT_BORDER,
        RV_PCBOOT_TITLE_HEIGHT };
    fill_rect(target, title, RV_PCBOOT_TITLE_BAR);
    const int64_t title_x = title.x + RV_PCBOOT_TITLE_TEXT_X;
    const int64_t title_y = title.y + RV_PCBOOT_TITLE_TEXT_Y;
    const std::string_view title_text = rv_pctext(RV_PCBOOT_TITLE_ID);
    if (text_fits(title, title_x, title_y, title_text)) {
        draw_text(target, title_x, title_y, title_text, RV_PCBOOT_WHITE);
    }

    // Hourglass, then the starting line beside it.
    const int64_t body_top = title.y + title.h + RV_PCBOOT_BODY_GAP;
    const int64_t glass_x = window.x + RV_PCBOOT_GLASS_X;
    draw_hourglass(target, glass_x, body_top);
    const int64_t text_x = glass_x + RV_PCBOOT_GLASS_COLUMNS * RV_PCBOOT_GLASS_SCALE + RV_PCBOOT_GLASS_GAP;
    const rv_pcboot_rect window_body = { window.x + RV_PCBOOT_BORDER,
        body_top,
        window.w - 2 * RV_PCBOOT_BORDER,
        window.y + window.h - RV_PCBOOT_BORDER - body_top };
    const int64_t starting_y = body_top + (RV_PCBOOT_GLASS_HEIGHT - RV_PCBOOT_CELL) / 2;
    const std::string_view starting = rv_pctext(RV_PCBOOT_STARTING_ID);
    if (text_fits(window_body, text_x, starting_y, starting)) {
        draw_text(target, text_x, starting_y, starting, RV_PCBOOT_BLACK);
    }

    // Journal lines in entry order; the journal stops at the first line that does not fit.
    const rv_pcboot_rect button = rv_pcboot_button_rect(width, height);
    const int64_t lines_top = body_top + RV_PCBOOT_GLASS_HEIGHT + RV_PCBOOT_LINES_GAP;
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
    return RV_OK;
}

} // namespace rv_3dmppc
