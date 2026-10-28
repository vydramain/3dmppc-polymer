#pragma once

// mppcinspector's run: the disc's source rules, findings to stdout, and an optional report file.
// The exit code is the caller's; this reports what the run found.

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
// An old regular report is removed first, so a report exists only after a successful run.
int rv_inspector_run(const rv_inspector_options &options, bool &has_error, std::string &error);

} // namespace rv_pdktools
