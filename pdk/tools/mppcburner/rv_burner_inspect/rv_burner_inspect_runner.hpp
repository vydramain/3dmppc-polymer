#pragma once

#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// --- the inspect subcommand ---
//
// Reports what is inside an already-burned .mppcdisc. Nothing is unpacked and
// nothing is written: it is the answer to "what did I just burn?" and to "why
// will the console not take this?".
//
// Its listing goes to STDOUT, alone, so it pipes into grep and diff. Diagnostics
// go to stderr like every other command's. That split is the whole reason this
// is a separate subcommand rather than extra output from a burn.

/// Print the manifest and the entry sizes of a .mppcdisc.
///
/// Reads `options.operand` as the image to inspect.
///
/// @param options  the parsed command line
/// @return a process exit code: 0 on success, 1 when the file cannot be read
int rv_burner_inspect_run(const rv_burner_options &options);

} // namespace rv_pdktools
