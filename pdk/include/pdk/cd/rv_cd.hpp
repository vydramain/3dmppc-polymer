#pragma once

#include <cstdint>

namespace rv_pdk
{

class rv_cd
{
public:
	virtual ~rv_cd() = default;

	virtual int64_t asset_open(const char *resname) = 0;
	virtual int64_t asset_size(int64_t handle) = 0;
	virtual int64_t asset_read(int64_t handle, void *baddr, int64_t baddr_size) = 0;
};

} // namespace rv_pdk
