#pragma once

#include <cstdint>

namespace rv_pdk
{

class rv_cm
{
public:
	virtual ~rv_cm() = default;

	virtual int64_t card_slots() = 0;
	virtual int64_t card_slot_size() = 0;
	virtual int64_t card_size(int64_t slot) = 0;
	virtual int64_t card_read(int64_t slot, void *baddr, int64_t baddr_size) = 0;
	virtual int64_t card_write(int64_t slot, const void *data, int64_t data_size) = 0;
	virtual int64_t card_erase(int64_t slot) = 0;
};

} // namespace rv_pdk
