#pragma once

#include <cstdint>

#include "pdk/cio/rv_imouse.hpp"
#include "pdk/cio/rv_isource.hpp"
#include "pdk/cio/rv_ohaptic.hpp"

namespace rv_pdk
{

class rv_cio
{
public:
	virtual ~rv_cio() = default;

	virtual int64_t iport_count() = 0;
	virtual rv_istate iport_state(int64_t port) = 0;
	virtual uint64_t iport_abilities(int64_t port) = 0;

	virtual rv_imouse imouse() = 0;

	virtual int64_t ohaptic(int64_t port, rv_oheffect effect) = 0;
};

} // namespace rv_pdk
