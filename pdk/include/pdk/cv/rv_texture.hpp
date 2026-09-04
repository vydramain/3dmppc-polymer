#pragma once

#include <cstdint>

namespace rv_pdk
{

enum rv_texfmt {
	RV_TEXFMT_IDX4 = 4,      // 4-bit palette index  (16-colour palette)
	RV_TEXFMT_IDX8 = 8,      // 8-bit palette index  (256-colour palette)
	RV_TEXFMT_DIRECT15 = 15, // 15-bit direct colour + STP bit (no palette)
};

struct rv_texture {
	rv_texfmt format; // how to read `data` (and whether a palette is needed)

	const void *data; // texel bytes, owned by the game (main RAM)
	uint64_t size;    // length of `data`, in bytes
	uint64_t width;   // texel columns
	uint64_t height;  // texel rows
};

} // namespace rv_pdk
