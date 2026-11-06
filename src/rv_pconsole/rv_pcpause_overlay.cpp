// The pause picture: the label, the dimming and the centring. The glyphs come
// from rv_pchost_text.hpp.
#include "rv_pconsole/rv_pcpause_overlay.hpp"

#include <cstddef>
#include <string_view>

#include "pdklib/rv_font/rv_font_data.hpp"
#include "rv_pconsole/rv_pchost_text.hpp"

namespace rv_3dmppc
{
namespace
{

// What a stopped machine says. The console's own words, not the disc's - which
// is why it is spelled CONSOLE and not GAME: what stopped is the frame loop,
// and the disc is not paused so much as simply not being called.
constexpr std::string_view RV_PCONSOLE_PAUSE_LABEL = "CONSOLE PAUSED";
constexpr int RV_PCONSOLE_PAUSE_LABEL_SCALE = 2;

// Pause picture constants: colours, dimming mask and centring divisor.
constexpr uint32_t OPAQUE_BLACK = 0xFF000000u;         // Opaque black for pause overlay background
constexpr uint32_t HALF_BRIGHTNESS_MASK = 0x007F7F7Fu; // Mask for half brightness after >>1
constexpr uint32_t OPAQUE_WHITE = 0xFFFFFFFFu;         // Opaque white for pause label text
constexpr int CENTER_DIVISOR = 2;                      // Divisor for centering text horizontally and vertically

} // namespace

// The picture a stopped console presents: the last frame, dimmed, with the
// label across the middle.
//
// A COPY, never the disc's own framebuffer. Drawing into that would make the
// pause destructive - the text would end up in --dump-frame, and a second pause
// would print over the first - and the disc's last frame has to stay exactly
// what the disc drew.
void rv_pcpause_overlay_build(std::vector<uint32_t> &out, const uint32_t *frame, int64_t width, int64_t height)
{
    const std::size_t pixels = static_cast<std::size_t>(width * height);
    out.assign(pixels, OPAQUE_BLACK);
    if (frame != nullptr) {
        for (std::size_t i = 0; i < pixels; ++i) {
            // Halved, not blacked out. The developer still needs to see WHAT is
            // on screen when they stopped it; the dimming is what keeps white
            // text readable over a bright frame.
            out[i] = OPAQUE_BLACK | ((frame[i] >> 1) & HALF_BRIGHTNESS_MASK);
        }
    }

    // Centred on the ink, not on the cells: see rv_pchost_text_ink_width.
    const int scale = RV_PCONSOLE_PAUSE_LABEL_SCALE;
    const int64_t text_width = rv_pchost_text_ink_width(RV_PCONSOLE_PAUSE_LABEL, scale);
    const int64_t text_height = rv_pdklib::rv_font_ink_height * scale;
    const rv_pchost_text_target target{ out.data(), width, height };
    rv_pchost_text_draw(target,
        (width - text_width) / CENTER_DIVISOR,
        (height - text_height) / CENTER_DIVISOR,
        RV_PCONSOLE_PAUSE_LABEL,
        scale,
        OPAQUE_WHITE);
}

} // namespace rv_3dmppc
