#pragma once

// mppcrepairer's run: the disc check, the repair of what it found, the check again, and an optional report file.
// The exit code is the caller's; this reports what the run found.

#include <cstdio>
#include <filesystem>
#include <string>

namespace rv_pdktools
{

struct rv_repairer_options {
    std::filesystem::path disc;
    // Report file to write after the repair; empty - no report.
    std::filesystem::path report;
};

// RV_OK sets `has_error` to whether the check after the repair has an error finding. Otherwise returns the failing
// code, with `error` set to the reason. Codes:
// RV_ERR_NOENT: no disc; a file vanished during the run; no backup root ($XDG_STATE_HOME and $HOME both unset or
// relative) when a file has to be written (no copy is taken otherwise).
// RV_ERR_INVAL: the report exists and is not a regular file; the repair refused a fix outside the table, a path outside
// the disc, a non-regular file or a symlink target.
// RV_ERR_BUSY: a backup copy with this time stamp already exists; or no free temporary name beside a file to write.
// RV_ERR_IO: as in rv_inspector_run for the disc and the report; a file cannot be examined, copied or written,
// or a backup directory cannot be created.
// Pass through unchanged: the rule codes, and the other codes of rv_repair_apply and rv_repair_timestamp
// (for example a refused rule table or an empty stamp).
// A failed repair leaves the files it did not write byte for byte as they were; the files it wrote before the failure
// and their backup copies are printed to stdout. A file whose write failed is as it was, and its backup copy is named
// in `error`. When a backup cannot be taken, the target is untouched and `error` names what failed: the copy, its
// directory, the source, the disc, or the backup root.
// A failed run leaves no report, with rv_inspector_run's exception.
int rv_repairer_run(const rv_repairer_options &options, bool &has_error, std::string &error);

// The usage text, to `stream`.
void rv_repairer_print_usage(std::FILE *stream);
// A failed run's error to stderr, as "mppcrepairer: <error>".
void rv_repairer_print_error(const std::string &error);
// Usage error for an operand count other than one: the reason with `got`, then the usage, to stderr.
void rv_repairer_print_operand_error(int got);
// Usage error for an empty --report PATH: the reason, then the usage, to stderr.
void rv_repairer_print_empty_report_error();

} // namespace rv_pdktools
