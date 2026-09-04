// What mppcburner says to the person who typed the command.
//
// The shape is getopt's, because getopt writes into the same stream and cannot
// be told otherwise:
//
//   mppcburner: invalid option -- 'z'          <- printed by getopt
//   mppcburner: expected one disc directory    <- printed by us
#pragma once

#include <string>

#include "pdklib/rv_stdio/rv_stdio.hpp"

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
    rv_pdklib::rv_fprintf(stderr, "%s: %s\n", rv_burner_progname(), message.c_str());
}

inline void rv_burner_print_warning(const std::string &message)
{
    rv_pdklib::rv_fprintf(stderr, "%s: warning: %s\n", rv_burner_progname(), message.c_str());
}

// One rung of the [n/4] ladder. No program prefix: this is progress, not a
// diagnostic, and the prefix would repeat four times for nothing.
inline void rv_burner_print_step(int step, const char *label, const std::string &detail)
{
    rv_pdklib::rv_fprintf(stderr, "[%d/4] %-11s %s\n", step, label, detail.c_str());
}

inline std::string rv_burner_human_size(int64_t bytes)
{
    char buffer[64];
    const double value = static_cast<double>(bytes);
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%lld B", static_cast<long long>(bytes));
    } else if (bytes < 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", value / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", value / (1024.0 * 1024.0));
    }
    return std::string(buffer);
}

} // namespace rv_pdktools
