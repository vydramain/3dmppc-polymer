#pragma once

#include <cstdint>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pboot_mode.hpp"

namespace rv_3dmppc
{

// Can this machine provide what the disc's [budget] declares?
//
// The budget is not measured against any console constant — a disc may ask for
// far more than the built-in disc does. It is measured against the resources
// this machine actually has, and only for the subsystems that are switched on:
// a run with audio off neither checks nor counts the disc's sound memory.
//
// Returns RV_OK, or RV_ERR_INVAL after logging the resource, the amount
// required, the amount available, and why it was refused.
int64_t rv_pboot_check_budget(
    const rv_pdklib::rv_manifest_budget &budget,
    const rv_pboot_mode_info &machine);

}
