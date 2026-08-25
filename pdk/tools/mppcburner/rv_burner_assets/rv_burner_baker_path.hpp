#pragma once

#include <string>

namespace rv_pdktools
{

// --- finding mppcbaker ---
//
// mppcbaker is a separate PROGRAM the burner runs, not a library it links.
// Linking it would pull stb_image — an image decoder — into the burner, and the
// two tools are shipped, versioned and rebuilt independently; a subprocess keeps
// that boundary and lets `--baker` point at another build entirely. The cost is
// this file: a program has to be found before it can be run, whereas a function
// is simply called.

/// Locate the mppcbaker executable.
///
/// Searched in order: @p hint, then next to the running mppcbaker's own binary
/// (the usual case — both tools sit in one build tree), then $PATH.
///
/// @param hint   value of `--baker`; empty means search
/// @param out    receives the path of an executable file
/// @param error  set when @p hint is not executable, or nothing was found
/// @return true when @p out holds a runnable mppcbaker
bool find_baker(const std::string &hint, std::string &out, std::string &error);

} // namespace rv_pdktools
