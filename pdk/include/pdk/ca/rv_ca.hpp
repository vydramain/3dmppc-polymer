#pragma once

#include <cstdint>

#include "pdk/ca/rv_sample.hpp"
#include "pdk/ca/rv_voice_conf.hpp"

namespace rv_pdk
{

class rv_ca
{
public:
	virtual ~rv_ca() = default;

	virtual int64_t voice_count() = 0;

	virtual int64_t sound_memory_size() = 0;
	virtual int64_t sound_asset_malloc(int64_t size) = 0;
	virtual int64_t sound_asset_write(int64_t addr, const rv_sample *sample) = 0;
	virtual int64_t sound_asset_free(int64_t addr) = 0;

	virtual int64_t voice_setup(const rv_voice_conf *conf) = 0;
	virtual int64_t voice_play(int64_t voice_mask) = 0;
	virtual int64_t voice_stop(int64_t voice_mask) = 0;
	virtual int64_t voice_status(int64_t voice_mask) = 0;
};

} // namespace rv_pdk
