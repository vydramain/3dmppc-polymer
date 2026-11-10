// rv_dmain: drawing pieces of the olive screen - colour conversion, plates, fitted text, texts.
#pragma once

#include <cstdint>
#include <string_view>

#include "pdk/cv/rv_vertex.h"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_theme/rv_theme_olive.hpp"
#include "rv_dmain_draw_shared.hpp"

namespace rv_service
{

namespace rv_dmain_detail
{

// An integer rectangle in frame pixels.
struct rv_ui_rect {
    int x;
    int y;
    int w;
    int h;
};

constexpr int UI_CHANNEL_MAX_8 = 255; // 8-bit channel maximum
constexpr int UI_CHANNEL_MAX_5 = 31;  // RGB555 channel maximum
constexpr int UI_BITS_DROPPED = 3;    // the machine truncates 8 -> 5 bits with >> 3
constexpr int UI_BYTE_MASK = 0xff;    // one channel of 0xRRGGBB
constexpr int UI_SHIFT_R = 16;        // red position in 0xRRGGBB
constexpr int UI_SHIFT_G = 8;         // green position in 0xRRGGBB
constexpr int UI_BEVEL = rv_pdklib::rv_theme_bevel_px;
constexpr int UI_RINGS = UI_BEVEL;         // one bevel ring per bevel pixel
constexpr int UI_PLATE_MIN = 2 * UI_BEVEL; // a plate smaller than this has no face
constexpr int UI_BOLD_SHIFT = 1;           // bold = same text again one pixel right

// Nearest RGB555 value, passed as c5 << 3: the flat-fill dither and the >> 3 then leave it unchanged.
constexpr uint8_t ui_channel(int c8)
{
    const int c5 = (c8 * UI_CHANNEL_MAX_5 * 2 + UI_CHANNEL_MAX_8) / (UI_CHANNEL_MAX_8 * 2);
    return static_cast<uint8_t>(c5 << UI_BITS_DROPPED);
}

constexpr rv_color ui_color(uint32_t rgb)
{
    return rv_color{ ui_channel(static_cast<int>(rgb >> UI_SHIFT_R) & UI_BYTE_MASK),
        ui_channel(static_cast<int>(rgb >> UI_SHIFT_G) & UI_BYTE_MASK),
        ui_channel(static_cast<int>(rgb) & UI_BYTE_MASK) };
}

// One plate: face plus one bevel ring per bevel pixel (UI_RINGS) of four non-overlapping strips
// (at most 1 + 4 * UI_RINGS sprites). In a ring the top row and left column are `a`, the bottom row and
// right column `b`; the top-right and bottom-left corners are `b`. Nothing is filed for a plate narrower
// or lower than UI_PLATE_MIN.
template <typename Sink>
void ui_plate(uint32_t face, uint32_t a, uint32_t b, const rv_ui_rect &r, int32_t depth, Sink &&sink)
{
    if (r.w < UI_PLATE_MIN || r.h < UI_PLATE_MIN) {
        return;
    }
    const rv_color ca = ui_color(a);
    const rv_color cb = ui_color(b);
    const auto put = [&](int x, int y, int w, int h, rv_color c) {
        if (w <= 0 || h <= 0) {
            return;
        }
        sink(make_bar(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), c, depth));
    };

    put(r.x + UI_BEVEL, r.y + UI_BEVEL, r.w - 2 * UI_BEVEL, r.h - 2 * UI_BEVEL, ui_color(face));
    for (int i = 0; i < UI_RINGS; ++i) {
        const int left = r.x + i;
        const int top = r.y + i;
        const int right = r.x + r.w - 1 - i;
        const int bottom = r.y + r.h - 1 - i;
        put(left, top, right - left, 1, ca);         // top row without its right corner
        put(left, top + 1, 1, bottom - top - 1, ca); // left column without its corners
        put(left, bottom, right - left + 1, 1, cb);  // bottom row with both corners
        put(right, top, 1, bottom - top, cb);        // right column with the top-right corner
    }
}

template <typename Sink>
void ui_plate_raised(uint32_t face, const rv_ui_rect &r, int32_t depth, Sink &&sink)
{
    ui_plate(face, rv_pdklib::rv_theme_bevel_hi, rv_pdklib::rv_theme_bevel_lo, r, depth, sink);
}

template <typename Sink>
void ui_plate_sunken(uint32_t face, const rv_ui_rect &r, int32_t depth, Sink &&sink)
{
    ui_plate(face, rv_pdklib::rv_theme_bevel_lo, rv_pdklib::rv_theme_bevel_hi, r, depth, sink);
}

// Width without the blank spacing columns after the last glyph: fitting and centring use the ink.
// The texts are ASCII: the width counts characters.
inline int ui_ink_width(std::string_view text, int scale)
{
    const int advance = rv_pdklib::rv_font_measure_width(text, scale);
    if (advance == 0) {
        return 0;
    }
    return advance - (rv_pdklib::rv_font_cell_width - rv_pdklib::rv_font_ink_width) * rv_pdklib::rv_font_scale_clamp(scale);
}

// Text at the style's scale, drawn only when its ink width and one scaled glyph cell lie inside `area`.
// The text must be one line: only one cell height is checked, a '\n' would draw below the area.
template <typename Sink>
bool ui_text_fit(const rv_pdklib::rv_font_style &style,
    const rv_ui_rect &area,
    int x,
    int y,
    std::string_view text,
    int extra_w,
    Sink &&sink)
{
    const int w = ui_ink_width(text, style.scale) + extra_w;
    const int h = rv_pdklib::rv_font_line_height(style.scale);
    if (x < area.x || y < area.y || x + w > area.x + area.w || y + h > area.y + area.h) {
        return false;
    }
    rv_pdklib::rv_font_draw(style, x, y, text, sink);
    return true;
}

template <typename Sink>
bool ui_text(const rv_pdklib::rv_font_style &style, const rv_ui_rect &area, int x, int y, std::string_view text, Sink &&sink)
{
    return ui_text_fit(style, area, x, y, text, 0, sink);
}

template <typename Sink>
bool ui_text_bold(const rv_pdklib::rv_font_style &style,
    const rv_ui_rect &area,
    int x,
    int y,
    std::string_view text,
    Sink &&sink)
{
    if (!ui_text_fit(style, area, x, y, text, UI_BOLD_SHIFT, sink)) {
        return false;
    }
    rv_pdklib::rv_font_draw(style, x + UI_BOLD_SHIFT, y, text, sink);
    return true;
}

} // namespace rv_dmain_detail

// The disc's texts.
inline constexpr std::string_view UI_TITLE = "3dmppc";             // window title
inline constexpr std::string_view UI_NO_DISC = "No disc inserted"; // shown when the tray is empty
inline constexpr std::string_view UI_HINT_SOUND = "Z Sound";       // hint button: beep
inline constexpr std::string_view UI_HINT_POWER = "Esc Power off"; // hint button: power off

// Captions of the 12 test cells, in grid order.
inline constexpr std::string_view UI_TEST_CAPTIONS[] = { "Line",
    "Tri",
    "Quad",
    "Wire",
    "Sprite",
    "Direct15",
    "Tile",
    "Idx4",
    "Cutout",
    "Depth",
    "Stretch",
    "3D" };

// Self-test probe labels.
inline constexpr std::string_view UI_PROBE_LABELS[] = { "tex", "font", "audio", "drive", "card", "paths" };

// Probe verdicts, all 3 characters so the column stays aligned.
inline constexpr std::string_view UI_VERDICT_OK = "ok ";
inline constexpr std::string_view UI_VERDICT_ERR = "err";
inline constexpr std::string_view UI_VERDICT_NA = "n/a";

} // namespace rv_service
