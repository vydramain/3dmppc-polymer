#pragma once

#include <cstdint>

namespace rv_pdk
{

// The fully transparent texel. Not a colour: a hole.
constexpr uint16_t RV_TEXEL_TRANSPARENT = 0x0000;

// A colour already reduced to this machine's 5-bit-per-channel space — the space
// a DIRECT15 texel and a palette entry are actually stored in.
//
// It is a type of its own and not rv_color (rv_vertex.hpp) on purpose. Both are
// three bytes and they mean different things, so the compiler, not a comment,
// decides where the boundary is: anything that measures a distance between
// colours takes rv_color5, because measuring in 8-bit space would optimise a
// precision the hardware throws away a moment later.
struct rv_color5 {
	uint8_t r = 0; // 0..31
	uint8_t g = 0; // 0..31
	uint8_t b = 0; // 0..31
};

}; // namespace rv_pdk
