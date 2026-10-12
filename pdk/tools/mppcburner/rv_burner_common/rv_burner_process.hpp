#pragma once

#include <filesystem>
#include <string>

namespace rv_pdktools
{

// --- child processes ---
//
// The burner does not compile or bake anything itself: it drives cmake, ninja
// and the bakers as child processes. Everything needed to do that safely — quote
// a path, run a command, show what the child said — lives here, because every
// phase that spawns a child needs the same three things.

/// Quote a string for a POSIX shell: wrap it in single quotes, ending and
/// reopening them around any single quote inside.
///
/// Every path handed to a child goes through this. A disc directory may contain
/// a space, and a build that breaks on that is a build nobody trusts.
///
/// @param text  raw text, typically a filesystem path
/// @return one shell word that expands back to @p text verbatim
std::string shell_quote(const std::string &text);

/// Run @p command through the shell and collect what it printed.
///
/// stdout and stderr are merged and captured rather than inherited, so a
/// successful compile stays quiet and the [n/4] ladder stays readable. On
/// failure the caller hands the capture to dump_child_output().
///
/// @param command  a full shell command line, already quoted
/// @param output   receives the child's merged stdout and stderr
/// @return the child's exit status; 128 + signal if it was killed; -1 if it
///         could not be started at all
int run_capture(const std::string &command, std::string &output);

/// Print a captured child output to stderr exactly as it arrived, adding only a
/// closing newline if the child forgot one.
///
/// No prefixing and no reflowing: a compiler's own diagnostic is the most useful
/// sentence anyone could print about a broken disc, and rewriting it helps
/// nobody.
///
/// @param output  a capture from run_capture(); an empty one prints nothing
void dump_child_output(const std::string &output);

// --- the running program ---

/// Directory holding the running burner binary, read from /proc/self/exe.
///
/// Not the working directory and not argv[0], which the user or caller chose.
///
/// @return that directory, or an empty path if the OS does not say; the caller decides how to fall back
std::filesystem::path executable_directory();

} // namespace rv_pdktools
