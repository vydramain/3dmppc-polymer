// The built-in disc's budget.
//
// A disc declares what it needs in its manifest [budget], and that declaration
// is what it gets: the numbers become the machine it runs on. The built-in
// service test carries no manifest, so its requirements live here instead:
// the reference console's own answers, copied verbatim from
// docs/platform/specs.md, "Target Spec".
//
// This is not a ceiling on what other discs may ask for. What a disc may ask
// for is bounded by the machine it is asked to run on, and by nothing else.
#pragma once

#include <cstdint>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pboot_args.hpp"

namespace rv_3dmppc
{

class rv_pcloader;

inline const rv_pdklib::rv_manifest_budget &rv_pboot_budget_builtin()
{
    // A default-constructed budget IS the reference machine: pdklib carries
    // those numbers once (rv_manifest.hpp) and a manifest that states nothing
    // arrives holding exactly them. Restating them here would be a second list
    // free to drift from the one the burner fills discs from.
    static const rv_pdklib::rv_manifest_budget kBudget;
    return kBudget;
}

// What the machine is going to be. A disc declares its requirements in its
// manifest and those numbers are the machine it gets; the built-in service
// test carries no manifest, so it runs on the reference specification.
// Mounts the archive when a disc path was given. Returns
// RV_OK with `out` set, or a negative rv_err after logging the refusal.
int64_t rv_pboot_budget_select(const rv_pboot_args &args, rv_pcloader &loader,
    const rv_pdklib::rv_manifest_budget *&out);

} // namespace rv_3dmppc
