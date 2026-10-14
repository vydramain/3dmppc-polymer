// Implementation-side budget evaluation. Boot calls the selected class's
// evaluate() before anything is constructed: arithmetic on the declared
// numbers only, never an allocation and never a log line.
#pragma once

#include <cstdint>
#include <string>

#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_3dmppc
{

// Returns RV_OK and sets `bytes` to the peak host bytes the class allocates for the budget.
// Otherwise returns RV_ERR_* and sets `error` to the field and the cause; `error` stays empty on success.
// Called only after contract validation, so every field it reads is non-negative.
using rv_pcbudget_evaluate_fn = int (*)(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes, std::string &error);

// out = a * b for non-negative a and b. Overflow: RV_ERR_INVAL with `error` set, `out` untouched.
int rv_pcbudget_mul(const char *field, int64_t a, int64_t b, int64_t &out, std::string &error);

// total += amount. Negative amount or overflow: RV_ERR_INVAL with `error` set, `total` untouched.
int rv_pcbudget_add(int64_t &total, const char *field, int64_t amount, std::string &error);

} // namespace rv_3dmppc
