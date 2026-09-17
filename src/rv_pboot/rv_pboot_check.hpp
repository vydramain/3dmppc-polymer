#pragma once

#include <cstdint>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pboot_machine.hpp"

namespace rv_3dmppc
{

// Can this machine provide what the disc's [budget] declares?
//
// Contract validation (every field non-negative, every active
// field positive, voice_count within what a voice mask can name) is
// identical for every mode and every backend. What each byte actually costs
// is not decided here at all: every concrete slot class in `slots` statically
// evaluates its own peak host allocation for this budget (rv_pcbudget.hpp),
// BEFORE any of them is constructed, and this only sums those answers and
// compares the total against what the machine actually has.
//
// Returns RV_OK, or RV_ERR_INVAL after logging the resource, the amount
// required, the amount available, and why it was refused.
int64_t rv_pboot_check_budget(
    const rv_pdklib::rv_manifest_budget &budget,
    const rv_pcslots &slots,
    const rv_pboot_mode_info &machine);

}
