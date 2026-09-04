#pragma once

#include "pdk/ca/rv_ca.hpp"
#include "pdk/cd/rv_cd.hpp"
#include "pdk/cio/rv_cio.hpp"
#include "pdk/cm/rv_cm.hpp"
#include "pdk/cv/rv_cv.hpp"
#include "pdk/cl/rv_cl.hpp"
#include "pdk/rv_err.hpp"   // IWYU pragma: keep (shared error vocabulary)
#include "pdk/de/rv_dv.hpp" // IWYU pragma: keep (version vocabulary)

namespace rv_pdk
{

class rv_pdko
{
public:
	virtual ~rv_pdko() = default;

	virtual rv_ca *ca() = 0;   // sound chip (low-level SPU)
	virtual rv_cd *cd() = 0;   // disc drive - reads the mounted .mppcdisc
	virtual rv_cm *cm() = 0;   // memory card - persistent save slots
	virtual rv_cio *cio() = 0; // gamepads + haptic output + mouse
	virtual rv_cv *cv() = 0;   // GPU / rasterizer
	virtual rv_cl *cl() = 0;
};

} // namespace rv_pdk
