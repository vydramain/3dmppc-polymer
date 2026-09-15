// Implementation-side budget evaluation. Boot calls the selected class's
// evaluate() before anything is constructed: arithmetic on the declared
// numbers only, never an allocation and never a log line.
#pragma once

#include <cstdint>
#include <string>

#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_3dmppc
{

// Success: `reason` empty and `bytes` the peak host bytes the class allocates
// for the budget. Refusal: `reason` names the field and the limit; the caller
// reports it.
struct rv_pcbudget_cost {
    int64_t bytes = 0;
    std::string reason;
};

// Called only after contract validation, so every field it reads is
// non-negative.
using rv_pcbudget_evaluate_fn = rv_pcbudget_cost (*)(const rv_pdklib::rv_manifest_budget &budget);

// out = a * b for non-negative a and b. On overflow sets cost.reason, leaves
// `out` untouched and returns true.
bool rv_pcbudget_mul(rv_pcbudget_cost &cost, const char *field, int64_t a, int64_t b, int64_t &out);

// cost.bytes += amount. On a negative amount or an overflow sets cost.reason,
// leaves cost.bytes untouched and returns true.
bool rv_pcbudget_add(rv_pcbudget_cost &cost, const char *field, int64_t amount);

} // namespace rv_3dmppc
