#pragma once

// mppcinspector's run: the disc's source rules, findings to stdout, and an optional report file.
// The exit code is the caller's; this reports what the run found.

#include <cstdio>
#include <filesystem>
#include <string>

namespace rv_pdktools
{

struct rv_inspector_options {
    std::filesystem::path disc;
    // Report file to write; empty - no report.
    std::filesystem::path report;
};

// RV_OK sets `has_error` to whether any finding is an error. Otherwise returns the failing code, `error` set.
// RV_ERR_NOENT: `disc` missing or not a directory. RV_ERR_IO: `disc` cannot be examined, or the report cannot be
// removed, examined or written. RV_ERR_INVAL: `report` exists and is not a regular file. Rule codes pass through.
// An old regular report is removed first. A failed run leaves no report, unless a report file it had to remove
// (the old one, or a partial one) could not be removed: then the run fails with RV_ERR_IO and names it.
int rv_inspector_run(const rv_inspector_options &options, bool &has_error, std::string &error);

// The usage text, to `stream`.
void rv_inspector_print_usage(std::FILE *stream);
// A failed run's error to stderr, as "mppcinspector: <error>".
void rv_inspector_print_error(const std::string &error);
// Usage error for an operand count other than one: the reason with `got`, then the usage, to stderr.
void rv_inspector_print_operand_error(int got);
// Usage error for an empty --report PATH: the reason, then the usage, to stderr.
void rv_inspector_print_empty_report_error();

} // namespace rv_pdktools
