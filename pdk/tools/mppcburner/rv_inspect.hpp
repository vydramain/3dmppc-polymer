#pragma once

#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// Print what is inside an already-burned .mppcdisc, without unpacking it and
// without writing anything anywhere. The listing goes to stdout, errors to
// stderr. Same exit-code contract.
int rv_inspect_run(const rv_burner_options &options);

} // namespace rv_pdktools
