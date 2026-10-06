// rv_dmain: shared drawing helpers for test grid and POST.
#pragma once

#include <cmath>
#include <limits>

#include "pdk/cv/rv_cv.h"

namespace rv_service
{

namespace rv_dmain_detail
{

constexpr float ROUNDING_ADJUST = 0.5f;

inline int16_t to_screen(float value)
{
    const float rounded = std::floor(value + ROUNDING_ADJUST);
    if (rounded <= static_cast<float>(std::numeric_limits<int16_t>::min())) {
        return std::numeric_limits<int16_t>::min();
    }
    if (rounded >= static_cast<float>(std::numeric_limits<int16_t>::max())) {
        return std::numeric_limits<int16_t>::max();
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
