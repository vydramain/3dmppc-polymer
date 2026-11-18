#pragma once

// mppcinspector's run: the disc's source rules, findings to stdout, and an optional report file.
// The exit code is the caller's; this reports what the run found.

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace rv_pdktools
{

// The name every protocol line starts with.
inline constexpr const char *k_inspector_tool = "mppcinspector";

// The steps of a run, in order; the protocol prints them as the plan.
inline const std::vector<std::string> k_inspector_steps = { "disc", "scenes" };

struct rv_inspector_options {
    std::filesystem::path disc;
    // Report file to write; empty - no report.
    std::filesystem::path report;
};

// RV_OK sets `errors` to the number of error findings. Otherwise returns the failing code, `error` set.
// RV_ERR_NOENT: `disc` missing or not a directory. RV_ERR_IO: `disc` cannot be examined, or the report cannot be
// removed, examined or written. RV_ERR_INVAL: `report` exists and is not a regular file. Rule codes pass through.
// An old regular report is removed first. A failed run leaves no report, unless a report file it had to remove
// (the old one, or a partial one) could not be removed: then the run fails with RV_ERR_IO and names it.
int rv_inspector_run(const rv_inspector_options &options, size_t &errors, std::string &error);

// The usage text, to `stream`.
void rv_inspector_print_usage(std::FILE *stream);
// A failed run's error to stderr, as "mppcinspector: <error>".
void rv_inspector_print_error(const std::string &error);
// Usage error for an operand count other than one: the reason with `got`, then the usage, to stderr.
void rv_inspector_print_operand_error(int got);
// Usage error for an empty --report PATH: the reason, then the usage, to stderr.
void rv_inspector_print_empty_report_error();

} // namespace rv_pdktools
