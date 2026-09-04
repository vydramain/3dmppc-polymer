#pragma once

#include "pdk/rv_pdko.hpp"

namespace rv_pdk
{

class rv_de
{
public:
	virtual ~rv_de() = default;

	virtual int64_t disc_initialize(rv_pdko &pdk) = 0;

	virtual void frame_update(float dt) = 0;
	virtual void frame_render() = 0;

	virtual bool disc_release() const
	{
		return false;
	}
	virtual void disc_shutdown()
	{
	}
	virtual const char *disc_title() const = 0;
};

} // namespace rv_pdk
