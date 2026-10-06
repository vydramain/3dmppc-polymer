// rv_dmain: shared drawing helpers for test grid and POST.
#pragma once

#include <cmath>

#include "pdk/cv/rv_cv.h"

namespace rv_service
{

namespace rv_dmain_detail
{

inline int16_t to_screen(float value)
{
    const float rounded = std::floor(value + 0.5f);
    if (rounded <= -32768.0f) {
        return -32768;
    }
    if (rounded >= 32767.0f) {
        return 32767;
    }
    return static_cast<int16_t>(rounded);
}

// A flat rectangle, filled in completely so no byte of the union is left
// indeterminate. Factory method - rv_primitive is a tagged union of
// constructor-less structs, and an "almost filled" literal leaves live fields
// holding whatever the stack had.
inline rv_primitive make_bar(float x, float y, float w, float h, rv_color color, int32_t depth)
{
    rv_primitive primitive{};
    primitive.type = RV_PRIMITIVE_SPRITE;
    primitive.depth = depth;

    rv_sprite &sprite = primitive.data.sprite;
    sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
    sprite.addr_texture = 0;
    sprite.addr_palette = 0;
    sprite.color = color;
    sprite.mapping = RV_TEXWRAP_CLAMP;
    sprite.x = to_screen(x);
    sprite.y = to_screen(y);
    sprite.width = static_cast<uint16_t>(w < 0.0f ? 0.0f : w);
    sprite.height = static_cast<uint16_t>(h < 0.0f ? 0.0f : h);
    return primitive;
}

} // namespace rv_dmain_detail

} // namespace rv_service
