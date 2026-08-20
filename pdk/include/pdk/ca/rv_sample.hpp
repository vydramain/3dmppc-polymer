#pragma once

#include <cstdint>

namespace rv_pdk
{

struct rv_sample {
	const void *data; // sample bytes, owned by the game (main RAM)
	int64_t size;     // length of `data`, in bytes
};

} // namespace rv_pdk
