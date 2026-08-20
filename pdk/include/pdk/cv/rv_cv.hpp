#pragma once

#include <cstdint>

#include "pdk/cv/rv_pipeline.hpp" // IWYU pragma: keep (frame_configure flag vocabulary)
#include "pdk/cv/rv_primitives.hpp"
#include "pdk/cv/rv_texture.hpp"
#include "pdk/cv/rv_vertex.hpp"

namespace rv_pdk
{

class rv_cv
{
public:
	virtual ~rv_cv() = default;

	virtual int64_t screen_width() = 0;
	virtual int64_t screen_height() = 0;

	virtual int64_t texture_max_width() = 0;
	virtual int64_t texture_max_height() = 0;

	virtual int64_t video_memory_size() = 0;

	virtual int64_t video_asset_malloc(int64_t size) = 0;
	virtual int64_t video_asset_write(int64_t addr, const rv_texture *texture) = 0;
	virtual int64_t video_asset_free(int64_t addr) = 0;

	virtual int64_t frame_capacity() = 0;
	virtual int64_t frame_configure(uint64_t config, rv_color clear_color) = 0;
	virtual int64_t frame_put(const rv_primitive *primitive) = 0;
	virtual int64_t frame_flush() = 0;
};

} // namespace rv_pdk
