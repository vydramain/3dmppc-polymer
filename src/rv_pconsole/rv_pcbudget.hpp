// Implementation-side budget evaluation. Boot stage E3 (rv_pboot_check.cpp)
// calls every slot's evaluate() through this shape BEFORE anything is
// constructed: it must answer with the peak host bytes that class will
// allocate for a budget, or refuse by name, using only arithmetic on the
// numbers already declared — never an actual allocation.
#pragma once

#include <cstdint>

#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_3dmppc
{

// Returns RV_OK with `bytes` set to the peak host bytes this class allocates
// for `budget`, or RV_ERR_INVAL after logging (tag "pccheck") the field and
// the implementation's own limit. Called only after contract validation has
// already passed, so every field it reads is non-negative.
using rv_pcbudget_evaluate_fn = int64_t (*)(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes);

// Non-negative a*b into `out`. Returns true (and logs `field` under
// "pccheck") on overflow, leaving `out` untouched.
bool rv_pcbudget_mul(const char *field, int64_t a, int64_t b, int64_t &out);

// total += amount. Returns true (and logs `field` under "pccheck") on
// overflow, leaving `total` untouched.
bool rv_pcbudget_add(const char *field, int64_t &total, int64_t amount);

} // namespace rv_3dmppc
