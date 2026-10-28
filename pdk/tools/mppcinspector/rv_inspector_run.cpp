#include "rv_inspector_run.hpp"

#include <algorithm>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "rv_inspector_disc.hpp"
#include "rv_inspector_report.hpp"

namespace rv_pdktools
{

namespace
{

// Printed once when a finding has a repair rule; mppcrepairer reads the same disc directory. The path is
// quoted so that one with spaces can be copied.
constexpr const char *k_repair_hint_format = "fix available: run mppcrepairer \"%s\"\n";
// The usage text, printed for --help and for a usage error.
constexpr const char *k_usage_text =
    "mppcinspector - check a disc directory's sources and print the findings\n"
    "\n"
    "Usage:\n"
    "  mppcinspector <disc-directory> [--report PATH]\n"
    "\n"
    "Options:\n"
    "  -r, --report PATH  a successful run writes PATH, replacing an old regular file;\n"
    "                     a failed run leaves no report, unless a report file it had to\n"
    "                     remove (the old one, or a partial one) could not be removed: then\n"
    "                     the run fails and names it. A PATH that exists and is not a regular\n"
    "                     file (link, directory, FIFO) is refused, and so is an empty PATH\n"
    "  -h, --help         print this text\n"
    "\n"
    "Exit status: 0 no error findings, 1 error findings, 2 usage error or failed run.\n";
// A failed run's error line: the program's name, then the error.
constexpr const char *k_error_format = "mppcinspector: %s\n";
// Reasons for a usage error that main finds before any run.
constexpr std::string_view k_operand_error_text = "expects one operand, the disc directory; got ";
constexpr std::string_view k_empty_report_error_text = "--report needs a non-empty PATH";

bool has_fix(std::span<const rv_pdklib::rv_inspect_finding> findings)
{
    return std::any_of(findings.begin(), findings.end(), [](const rv_pdklib::rv_inspect_finding &f) {
        return !f.fix.empty();
    });
}

} // namespace

int rv_inspector_run(const rv_inspector_options &options, bool &has_error, std::string &error)
{
    if (!options.report.empty()) {
        const int old_rc = rv_inspector_report_clear(options.report, error);
        if (old_rc != RV_OK) {
            return old_rc;
        }
    }

    std::vector<rv_pdklib::rv_inspect_finding> findings;
    if (const int rc = rv_inspector_collect(options.disc, findings, error); rc != RV_OK) {
        return rc;
    }

    rv_inspector_print_findings(findings);
    rv_inspector_print_summary(findings);
    if (has_fix(findings)) {
        rv_pdklib::rv_fprintf(stdout, k_repair_hint_format, options.disc.string().c_str());
    }

    if (!options.report.empty()) {
        const int rc = rv_inspector_report_write(options.report, findings, error);
        if (rc != RV_OK) {
            return rc;
        }
    }

    has_error = rv_pdklib::rv_inspect_has_error(findings);
    return RV_OK;
}

void rv_inspector_print_usage(std::FILE *stream)
{
    rv_pdklib::rv_fprintf(stream, "%s", k_usage_text);
}

void rv_inspector_print_error(const std::string &error)
{
    rv_pdklib::rv_fprintf(stderr, k_error_format, error.c_str());
}

void rv_inspector_print_operand_error(int got)
{
    rv_inspector_print_error(std::string(k_operand_error_text) + std::to_string(got));
    rv_inspector_print_usage(stderr);
}

void rv_inspector_print_empty_report_error()
{
    rv_inspector_print_error(std::string(k_empty_report_error_text));
    rv_inspector_print_usage(stderr);
}

} // namespace rv_pdktools
