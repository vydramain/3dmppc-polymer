#pragma once

#include <string>

namespace rv_pdktools
{

// POSIX shell quoting: wrap in single quotes, and end/reopen them around any
// single quote in the payload. Every path we hand to a child goes through here,
// because a disc directory is allowed to have a space in its name and a build
// that breaks on that is a build nobody trusts.
std::string shell_quote(const std::string &text);

// Run `command`, collecting its stdout AND stderr into `output`. Returns the
// exit status, or -1 when the child could not be started at all.
//
// The output is captured rather than inherited so a successful compile stays
// quiet and the [n/4] ladder remains readable; on failure the caller dumps what
// it caught to stderr verbatim, because a compiler's own diagnostic is the most
// useful sentence anyone could print about a broken disc.
int run_capture(const std::string &command, std::string &output);

// Print a child's captured output as it was, with no prefixing or reflowing.
void dump_child_output(const std::string &output);

} // namespace rv_pdktools
