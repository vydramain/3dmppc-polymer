// The label, the dimming, and the one thing this file needs from pdklib: the
// glyph bitmaps.
//
// Only the DATA is borrowed. pdklib's own text drawer paints through rv_cv
// primitives and needs the font uploaded into virtual VRAM, which is a disc's
// job - using it here would mean the console allocating video memory behind the
// disc's back to print one word. The glyphs are what they are on the page
// (eight row bytes, top row first, high bit leftmost) and the destination is a
// host pixel buffer, so a blit is the whole of it.
#include "rv_pconsole/rv_pcpause_overlay.hpp"

#include <cstddef>
#include <string_view>

#include "pdklib/rv_font/rv_font_data.hpp"

namespace rv_3dmppc
{
namespace
{

// What a stopped machine says. The console's own words, not the disc's - which
// is why it is spelled CONSOLE and not GAME: what stopped is the frame loop,
// and the disc is not paused so much as simply not being called.
constexpr std::string_view RV_PCONSOLE_PAUSE_LABEL = "CONSOLE PAUSED";
constexpr int RV_PCONSOLE_PAUSE_LABEL_SCALE = 2;

// One line of pdklib's bitmap font, straight into an ARGB buffer.
//
// Only the DATA is borrowed from pdklib, not its text drawer: that one paints
// through rv_cv primitives and needs the font uploaded into virtual VRAM, which
// is a disc's job and would mean the console allocating video memory behind the
// disc's back to print one word. Here the glyphs are what they are on the page -
// eight row bytes, top row first, high bit leftmost - and the destination is the
// host's pixel buffer, so a blit is the whole of it.
// One set texel of a glyph, magnified: `scale` by `scale` pixels of one colour.
// Clipped rather than assumed to fit - the label is sized for 320x240 and a
// disc may declare a smaller screen.
void blit_texel(uint32_t *dst, int64_t width, int64_t height, int64_t x0, int64_t y0, int scale,
    uint32_t argb)
{
    for (int sy = 0; sy < scale; ++sy) {
        for (int sx = 0; sx < scale; ++sx) {
            const int64_t px = x0 + sx;
            const int64_t py = y0 + sy;
            if (px >= 0 && px < width && py >= 0 && py < height) {
                dst[py * width + px] = argb;
            }
        }
    }
}

void rv_pcpause_blit_text(uint32_t *dst, int64_t width, int64_t height, int64_t x0, int64_t y0,
    std::string_view text, int scale, uint32_t argb)
{
    int64_t pen = x0;
    for (const char c : text) {
        const int glyph = (c >= 32 && c <= 126) ? c - 32 : rv_pdklib::rv_font_notdef_index;
        const uint8_t *rows = &rv_pdklib::rv_font_bits[glyph * rv_pdklib::rv_font_cell_height];
        for (int row = 0; row < rv_pdklib::rv_font_cell_height; ++row) {
            for (int column = 0; column < rv_pdklib::rv_font_ink_width; ++column) {
                if ((rows[row] & (0x80u >> column)) != 0) {
                    blit_texel(dst, width, height, pen + column * scale, y0 + row * scale, scale,
                        argb);
                }
            }
        }
        pen += rv_pdklib::rv_font_cell_width * scale;
    }
}

} // namespace

// The picture a stopped console presents: the last frame, dimmed, with the
// label across the middle.
//
// A COPY, never the disc's own framebuffer. Drawing into that would make the
// pause destructive - the text would end up in --dump-frame, and a second pause
// would print over the first - and the disc's last frame has to stay exactly
// what the disc drew.
void rv_pcpause_overlay_build(std::vector<uint32_t> &out, const uint32_t *frame,
    int64_t width, int64_t height)
{
    const std::size_t pixels = static_cast<std::size_t>(width * height);
    out.assign(pixels, 0xFF000000u);
    if (frame != nullptr) {
        for (std::size_t i = 0; i < pixels; ++i) {
            // Halved, not blacked out. The developer still needs to see WHAT is
            // on screen when they stopped it; the dimming is what keeps white
            // text readable over a bright frame.
            out[i] = 0xFF000000u | ((frame[i] >> 1) & 0x007F7F7Fu);
        }
    }

    // The font's three trailing columns are letter spacing, so the last cell
    // carries blank width that must come off before centring - otherwise the
    // line sits a few pixels left of centre.
    const int scale = RV_PCONSOLE_PAUSE_LABEL_SCALE;
    const int64_t text_width =
        static_cast<int64_t>(RV_PCONSOLE_PAUSE_LABEL.size()) * rv_pdklib::rv_font_cell_width * scale -
        (rv_pdklib::rv_font_cell_width - rv_pdklib::rv_font_ink_width) * scale;
    const int64_t text_height = rv_pdklib::rv_font_ink_height * scale;
    rv_pcpause_blit_text(out.data(), width, height, (width - text_width) / 2,
        (height - text_height) / 2, RV_PCONSOLE_PAUSE_LABEL, scale, 0xFFFFFFFFu);
}

} // namespace rv_3dmppc
