#pragma once

#include <cstdint>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_infra/rv_pcmachine.hpp"

namespace rv_3dmppc
{

// Stage E3: can THIS machine provide what the disc's [budget] declares?
//
// The budget is not measured against any console constant — a disc may ask for
// far more than the built-in disc does. It is measured against the resources
// this machine actually has, and only for the subsystems that are switched on:
// a run with audio off neither checks nor counts the disc's sound memory.
//
// Returns RV_OK, or RV_ERR_INVAL after logging the resource, the amount
// required, the amount available, and why it was refused.
int64_t rv_compatibility_check_launch_disc(
    const rv_pdklib::rv_manifest_budget &budget,
    const rv_pcmachine_info &machine);

}
