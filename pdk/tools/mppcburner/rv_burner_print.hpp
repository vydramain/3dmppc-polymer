// What mppcburner says to the person who typed the command.
//
// The shape is getopt's, because getopt writes into the same stream and cannot
// be told otherwise:
//
//   mppcburner: invalid option -- 'z'          <- printed by getopt
//   mppcburner: expected one disc directory    <- printed by us
#pragma once

#include <string>
#include <vector>

#include "pdklib/rv_progress/rv_progress.hpp"
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

// The last error printed, so the [end] line of a command can name what went wrong.
inline std::string &rv_burner_last_error()
{
    static std::string message;
    return message;
}

inline void rv_burner_print_error(const std::string &message)
{
    rv_burner_last_error() = message;
    rv_pdklib::rv_fprintf(stderr, "%s: %s\n", rv_burner_progname(), message.c_str());
}

inline void rv_burner_print_warning(const std::string &message)
{
    rv_pdklib::rv_fprintf(stderr, "%s: warning: %s\n", rv_burner_progname(), message.c_str());
}

// The steps of the running command, set by its runner; rv_burner_print_step numbers a rung by its place here.
inline std::vector<std::string> &rv_burner_ladder()
{
    static std::vector<std::string> labels;
    return labels;
}

// One rung of the running command's ladder, numbered from its list (see rv_burner_steps.hpp).
inline void rv_burner_print_step(const std::string &label, const std::string &detail)
{
    const std::vector<std::string> &labels = rv_burner_ladder();
    std::size_t index = 0;
    while (index < labels.size() && labels[index] != label) {
        ++index;
    }
    rv_pdklib::rv_progress_step(rv_burner_progname(),
        static_cast<int>(index + 1),
        static_cast<int>(labels.size()),
        label,
        detail);
}

// Runs a command under the shared protocol: [plan] first, then its code, then exactly one [end] line.
template <typename Body>
int rv_burner_run_with_progress(const std::vector<std::string> &labels, Body body)
{
    rv_burner_ladder() = labels;
    rv_burner_last_error().clear();
    rv_pdklib::rv_progress_plan(rv_burner_progname(), labels);
    const int rc = body();
    if (rc == 0) {
        rv_pdklib::rv_progress_end_ok(rv_burner_progname());
    } else {
        const std::string &why = rv_burner_last_error();
        rv_pdklib::rv_progress_end_failed(rv_burner_progname(), why.empty() ? "exit code " + std::to_string(rc) : why);
    }
    return rc;
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
