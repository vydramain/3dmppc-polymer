#pragma once

#include <cstdint>

#include "pdk/cv/rv_vertex.hpp"

namespace rv_pdk
{

struct rv_line {
	rv_vertex vertexes[2];
};

enum rv_primitive_fill_mode {
	RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED = 1,
	RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE = 2,
	RV_PRIMITIVE_FILL_MODE_WIREFRAME = 3,
};

enum rv_texture_mapping_type {
	RV_TEXWRAP_CLAMP = 1,   // clamp to the edge texel
	RV_TEXWRAP_TILE = 2,    // repeat
	RV_TEXWRAP_STRETCH = 3, // stretch the texture across the primitive
};

struct rv_polygon {
	uint32_t fill_mode; // rv_primitive_fill_mode

	int64_t addr_texture;            // texture data in video RAM (if sampling)
	int64_t addr_palette;            // palette in video RAM (indexed formats)
	rv_texture_mapping_type mapping; // how to sample outside the texture

	uint32_t vertex_count; // 3 = triangle, 4 = quad; anything else = RV_ERR_INVAL
	rv_vertex vertexes[4]; // vertexes[3] is ignored when vertex_count == 3
};

struct rv_sprite {
	uint32_t fill_mode; // rv_primitive_fill_mode

	int64_t addr_texture; // texture data in video RAM (if sampling)
	int64_t addr_palette; // palette in video RAM (indexed formats)

	rv_color color;
	rv_texture_mapping_type mapping; // how to sample outside the texture

	int16_t x, y;           // upper-left corner (signed: may start off-screen)
	uint16_t width, height; // extent in pixels
};

enum rv_primitive_type {
	RV_PRIMITIVE_LINE = 1,
	RV_PRIMITIVE_POLYGON = 2,
	RV_PRIMITIVE_SPRITE = 3,
};

struct rv_primitive {
	uint32_t type; // rv_primitive_type: selects the union member
	int32_t depth; // ordering-table sort key (larger = nearer / on top)

	union {
		rv_line line;
		rv_polygon polygon;
		rv_sprite sprite;
	} data;
};

} // namespace rv_pdk
