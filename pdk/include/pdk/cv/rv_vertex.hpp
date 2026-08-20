#pragma once

#include <cstdint>

namespace rv_pdk
{

struct rv_color {
	uint8_t r, g, b;
};

struct rv_uv {
	uint16_t u, v;
};

struct rv_vertex {
	int16_t x, y;
	rv_color color;
	rv_uv uv;
};

} // namespace rv_pdk
