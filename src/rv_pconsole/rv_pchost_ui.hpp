// Olive drawing primitives for the console's own screens into a host ARGB buffer:
// clipped fill, the 2-pixel bevel edge, raised and sunken plates.
#pragma once

#include <algorithm>
#include <cstdint>

#include "pdklib/rv_theme/rv_theme_olive.hpp"
#include "rv_pconsole/rv_pchost_text.hpp"

namespace rv_3dmppc
{

// Rectangle in host frame pixels, top-left origin, w and h its size; it may lie partly or wholly outside the target.
struct rv_pchost_rect {
    int64_t x;
    int64_t y;
    int64_t w;
    int64_t h;
};

// Opaque alpha: theme tokens are 0xRRGGBB, the host buffer is 0xAARRGGBB.
inline constexpr uint32_t RV_PCHOST_UI_OPAQUE = 0xFF000000u;

// A plate needs room for two bevel rings on each side to have any face at all.
inline constexpr int64_t RV_PCHOST_UI_MIN_PLATE = 2 * rv_pdklib::rv_theme_bevel_px;

constexpr uint32_t rv_pchost_ui_argb(uint32_t token)
{
    return RV_PCHOST_UI_OPAQUE | token;
}

// Bevel light and shade as host words, so the plates read them by name.
inline constexpr uint32_t RV_PCHOST_UI_BEVEL_HI = rv_pchost_ui_argb(rv_pdklib::rv_theme_bevel_hi);
inline constexpr uint32_t RV_PCHOST_UI_BEVEL_LO = rv_pchost_ui_argb(rv_pdklib::rv_theme_bevel_lo);

// Fill clipped to the target; an empty or negative rectangle draws nothing.
inline void rv_pchost_ui_fill(const rv_pchost_text_target &target, const rv_pchost_rect &rect, uint32_t argb)
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

// Bevel rings, counted inward: top row and left column in `a`, then bottom row and
// right column in `b`, so the top-right and bottom-left corners end up `b`.
inline void rv_pchost_ui_edge(const rv_pchost_text_target &target, const rv_pchost_rect &rect, uint32_t a, uint32_t b)
{
    if (rect.w < RV_PCHOST_UI_MIN_PLATE || rect.h < RV_PCHOST_UI_MIN_PLATE) {
        return;
    }
    for (int64_t i = 0; i < rv_pdklib::rv_theme_bevel_px; ++i) {
        const int64_t w = rect.w - 2 * i;
        const int64_t h = rect.h - 2 * i;
        const int64_t x = rect.x + i;
        const int64_t y = rect.y + i;
        rv_pchost_ui_fill(target, { x, y, w, 1 }, a);
        rv_pchost_ui_fill(target, { x, y, 1, h }, a);
        rv_pchost_ui_fill(target, { x, y + h - 1, w, 1 }, b);
        rv_pchost_ui_fill(target, { x + w - 1, y, 1, h }, b);
    }
}

// `face` is a host ARGB word (for example rv_pchost_ui_argb(token)); the bevel colours are theme tokens converted above.
// Face colour with the light edge top and left.
inline void rv_pchost_ui_raised(const rv_pchost_text_target &target, const rv_pchost_rect &rect, uint32_t face)
{
    if (rect.w < RV_PCHOST_UI_MIN_PLATE || rect.h < RV_PCHOST_UI_MIN_PLATE) {
        return;
    }
    rv_pchost_ui_fill(target, rect, face);
    rv_pchost_ui_edge(target, rect, RV_PCHOST_UI_BEVEL_HI, RV_PCHOST_UI_BEVEL_LO);
}

// `face` is a host ARGB word (for example rv_pchost_ui_argb(token)); the bevel colours are theme tokens converted above.
// Face colour with the shade edge top and left.
inline void rv_pchost_ui_sunken(const rv_pchost_text_target &target, const rv_pchost_rect &rect, uint32_t face)
{
    if (rect.w < RV_PCHOST_UI_MIN_PLATE || rect.h < RV_PCHOST_UI_MIN_PLATE) {
        return;
    }
    rv_pchost_ui_fill(target, rect, face);
    rv_pchost_ui_edge(target, rect, RV_PCHOST_UI_BEVEL_LO, RV_PCHOST_UI_BEVEL_HI);
}

} // namespace rv_3dmppc
