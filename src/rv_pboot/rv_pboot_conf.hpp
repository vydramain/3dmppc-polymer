// The disc's numbers become the machine's.
#pragma once

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pboot_args.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// The disc's numbers become the machine's. Only the parameters that belong
// to this run rather than to the disc come from `args`; `slots` is the
// resolved choice of implementation for each swappable slot.
void rv_pboot_conf_build(
    const rv_pdklib::rv_manifest_budget &budget,
    const rv_pboot_args &args,
    const rv_pcslots &slots,
    rv_pconsole_conf &out);

} // namespace rv_3dmppc
