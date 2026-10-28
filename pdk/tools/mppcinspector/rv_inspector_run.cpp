#include "rv_inspector_run.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "rv_inspector_disc.hpp"

namespace rv_pdktools
{

namespace
{

constexpr std::string_view k_report_text = "cannot write report ";
constexpr std::string_view k_report_remove_text = "cannot remove old report ";
constexpr std::string_view k_report_look_text = "cannot examine report ";
constexpr std::string_view k_report_not_regular_text = "not a regular file: ";
constexpr std::string_view k_partial_remove_text = "; cannot remove the partial report: ";
// errno MAY be 0 after a short fwrite: glibc sets it, the standard does not promise either way. strerror(0) is "Success".
constexpr std::string_view k_short_write_text = "short write";
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

// Code 0: errno was not set. A short write MAY leave it 0 (see k_short_write_text).
int report_error(const std::filesystem::path &report, int code, std::string &error)
{
    const std::string reason = code == 0 ? std::string(k_short_write_text) : std::string(std::strerror(code));
    error = std::string(k_report_text) + report.string() + ": " + reason;
    return RV_ERR_IO;
}

// A failed write or close leaves no partial report, unless its removal fails; then that failure is added to `error` and the
// partial report stays.
int fail_partial(const std::filesystem::path &report, int code, std::string &error)
{
    const int rc = report_error(report, code, error);
    std::error_code ec;
    std::filesystem::remove(report, ec);
    if (ec) {
        error += std::string(k_partial_remove_text) + ec.message();
    }
    return rc;
}

// An old one goes before the rules run, so a failed run leaves no report, unless a report file it had to remove
// (the old one, or a partial one) could not be removed: then the run fails and names it. The path must be absent
// or a regular file: a link, a directory or a FIFO is refused before anything is changed.
int remove_old_report(const std::filesystem::path &report, std::string &error)
{
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::symlink_status(report, ec);
    const bool missing = st.type() == std::filesystem::file_type::not_found || ec == std::errc::no_such_file_or_directory;
    const bool absent = missing || ec == std::errc::not_a_directory;
    if (absent) {
        return RV_OK;
    }
    if (ec) {
        error = std::string(k_report_look_text) + report.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    if (st.type() != std::filesystem::file_type::regular) {
        error = std::string(k_report_not_regular_text) + report.string();
        return RV_ERR_INVAL;
    }
    std::filesystem::remove(report, ec);
    if (ec) {
        error = std::string(k_report_remove_text) + report.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

// A plain write: creates or replaces the file, even when there are no findings (an empty file is the clean answer).
int write_report(const std::filesystem::path &report,
    std::span<const rv_pdklib::rv_inspect_finding> findings,
    std::string &error)
{
    const std::string text = rv_pdklib::rv_inspect_report_format(findings);
    std::FILE *file = std::fopen(report.string().c_str(), "wb");
    if (file == nullptr) {
        return report_error(report, errno, error);
    }
    errno = 0;
    const size_t written = std::fwrite(text.data(), 1, text.size(), file);
    const int write_code = errno;
    if (written != text.size()) {
        std::fclose(file);
        return fail_partial(report, write_code, error);
    }
    if (std::fclose(file) != 0) {
        return fail_partial(report, errno, error);
    }
    return RV_OK;
}

} // namespace

int rv_inspector_run(const rv_inspector_options &options, bool &has_error, std::string &error)
{
    if (!options.report.empty()) {
        const int old_rc = remove_old_report(options.report, error);
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
        const int rc = write_report(options.report, findings, error);
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
