// What mppcburner says to the person who typed the command.
//
// The shape is getopt's, because getopt writes into the same stream and cannot
// be told otherwise:
//
//   mppcburner: invalid option -- 'z'          <- printed by getopt
//   mppcburner: expected one disc directory    <- printed by us
#pragma once

#include <cstdio>
#include <string>

#ifdef __GLIBC__
#include <cerrno> // IWYU pragma: keep - declares program_invocation_short_name
#endif

namespace rv_pdktools
{

// Taken from argv[0] rather than written down, so the prefix keeps matching
// getopt's after the binary is renamed.
inline const char *rv_burner_progname()
{
#ifdef __GLIBC__
	return program_invocation_short_name;
#else
	return "mppcburner";
#endif
}

inline void rv_burner_print_error(const std::string &message)
{
	std::fprintf(stderr, "%s: %s\n", rv_burner_progname(), message.c_str());
}

inline void rv_burner_print_warning(const std::string &message)
{
	std::fprintf(stderr, "%s: warning: %s\n", rv_burner_progname(), message.c_str());
}

inline void rv_burner_print_info(const std::string &message)
{
	std::fprintf(stdout, "%s: info: %s\n", rv_burner_progname(), message.c_str());
}

// One rung of the [n/4] ladder. No program prefix: this is progress, not a
// diagnostic, and the prefix would repeat four times for nothing.
inline void rv_burner_print_step(int step, const char *label, const std::string &detail)
{
	std::fprintf(stderr, "[%d/4] %-11s %s\n", step, label, detail.c_str());
}

} // namespace rv_pdktools
