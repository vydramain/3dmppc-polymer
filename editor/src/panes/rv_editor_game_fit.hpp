#pragma once

#include <algorithm>
#include <cmath>

#include "prefs/rv_editor_prefs.hpp"

namespace rv_editor
{

// Where the console's frame goes inside the Game tile's picture area, in pixels
// from the area's corner. The frame keeps its proportions and is never cut: a
// fixed or integer multiple that does not fit is lowered to what fits, and
// `reduced` says so.
struct rv_editor_game_view
{
    float x, y, w, h;
    float scale;
    bool reduced;
};

inline rv_editor_game_view rv_editor_game_place(int frame_w, int frame_h, float area_w, float area_h,
    rv_editor_game_scale mode)
{
    if (frame_w <= 0 || frame_h <= 0 || area_w <= 0.0f || area_h <= 0.0f) {
        return { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false };
    }
    const float fit = std::min(area_w / static_cast<float>(frame_w), area_h / static_cast<float>(frame_h));
    const float whole = std::floor(fit);
    float scale = fit;
    bool reduced = false;
    int wanted = 0;
    switch (mode) {
        case rv_editor_game_scale::fit: break;
        case rv_editor_game_scale::integer: wanted = static_cast<int>(std::max(1.0f, whole)); break;
        case rv_editor_game_scale::x1: wanted = 1; break;
        case rv_editor_game_scale::x2: wanted = 2; break;
        case rv_editor_game_scale::x3: wanted = 3; break;
    }
    if (wanted != 0) {
        // The wanted multiple, or the largest whole one that fits, or below 1x the fit.
        scale = static_cast<float>(wanted) <= fit ? static_cast<float>(wanted) : (whole >= 1.0f ? whole : fit);
        reduced = scale != static_cast<float>(wanted);
    }
    // A hair over the product before flooring: 240 * (500 / 240) is 499.99998 in float.
    const float w = std::floor(static_cast<float>(frame_w) * scale + 0.001f);
    const float h = std::floor(static_cast<float>(frame_h) * scale + 0.001f);
    return { std::floor((area_w - w) / 2.0f), std::floor((area_h - h) / 2.0f), w, h, scale, reduced };
}

} // namespace rv_editor
