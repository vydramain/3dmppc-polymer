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
#include "pdklib/rv_manifest/rv_manifest_inspect.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "pdkkit/scene/rv_scene_inspect.hpp"

namespace rv_pdktools
{

namespace
{

// Texts this program adds. A finding line adds its location, severity and labels around the rule's text, key,
// rule and fix, which are copied as given.
constexpr std::string_view k_no_disc_text = "no disc directory at ";
constexpr std::string_view k_not_dir_text = "not a directory: ";
constexpr std::string_view k_disc_stat_text = "cannot examine disc directory ";
constexpr std::string_view k_report_text = "cannot write report ";
constexpr std::string_view k_report_remove_text = "cannot remove old report ";
constexpr std::string_view k_report_look_text = "cannot examine report ";
constexpr std::string_view k_report_not_regular_text = "not a regular file: ";
constexpr std::string_view k_partial_remove_text = "; cannot remove the partial report: ";
// errno MAY be 0 after a short fwrite: glibc sets it, the standard does not promise either way. strerror(0) is "Success".
constexpr std::string_view k_short_write_text = "short write";
// Summary line: the error and warning counts.
constexpr const char *k_summary_format = "%zu error(s), %zu warning(s)\n";
// Printed once when a finding has a repair rule; mppcrepairer reads the same disc directory. The path is
// quoted so that one with spaces can be copied.
constexpr const char *k_repair_hint_format = "fix available: run mppcrepairer \"%s\"\n";
// Bracket labels of a finding line: "... [<rule>, key <key>, fix <fix>]".
constexpr std::string_view k_key_label = ", key ";
constexpr std::string_view k_fix_label = ", fix ";
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

// The rule tables in run order: the disc.toml rules, then the kit's.
constexpr std::span<const rv_pdklib::rv_inspect_rule> k_rule_tables[] = {
    rv_pdklib::rv_manifest_inspect_rules,
    rv_pdkkit::rv_scene_inspect_rules,
};

// Absent (or a non-directory component) is RV_ERR_NOENT; any other failure to look at the path is RV_ERR_IO.
int check_disc_dir(const std::filesystem::path &disc, std::string &error)
{
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::status(disc, ec);
    const bool missing = st.type() == std::filesystem::file_type::not_found || ec == std::errc::no_such_file_or_directory;
    const bool absent = missing || ec == std::errc::not_a_directory;
    if (ec && !absent) {
        error = std::string(k_disc_stat_text) + disc.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    if (absent) {
        error = std::string(k_no_disc_text) + disc.string();
        return RV_ERR_NOENT;
    }
    if (!std::filesystem::is_directory(st)) {
        error = std::string(k_not_dir_text) + disc.string();
        return RV_ERR_NOENT;
    }
    return RV_OK;
}

// "<file>[:<line>]: <severity>: <text> [<rule>[, key <key>][, fix <fix>]]"; line 0 is the whole file.
std::string format_finding(const rv_pdklib::rv_inspect_finding &f)
{
    const std::string severity(rv_pdklib::detail::rv_inspect_severity_words[static_cast<size_t>(f.severity)]);
    const std::string line_no = f.line == 0 ? std::string() : ":" + std::to_string(f.line);
    const std::string key_part = f.key.empty() ? std::string() : std::string(k_key_label) + f.key;
    const std::string fix_part = f.fix.empty() ? std::string() : std::string(k_fix_label) + f.fix;
    return f.file + line_no + ": " + severity + ": " + f.text + " [" + f.rule + key_part + fix_part + "]";
}

void print_findings(std::span<const rv_pdklib::rv_inspect_finding> findings)
{
    for (const rv_pdklib::rv_inspect_finding &f : findings) {
        rv_pdklib::rv_fprintf(stdout, "%s\n", format_finding(f).c_str());
    }
}

size_t count_severity(std::span<const rv_pdklib::rv_inspect_finding> findings, rv_pdklib::rv_inspect_severity severity)
{
    return static_cast<size_t>(
        std::count_if(findings.begin(), findings.end(), [severity](const rv_pdklib::rv_inspect_finding &f) {
            return f.severity == severity;
        }));
}

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

    const int dir_rc = check_disc_dir(options.disc, error);
    if (dir_rc != RV_OK) {
        return dir_rc;
    }

    std::vector<rv_pdklib::rv_inspect_finding> findings;
    for (const std::span<const rv_pdklib::rv_inspect_rule> table : k_rule_tables) {
        const int rc = rv_pdklib::rv_inspect_run(table, options.disc, findings, error);
        if (rc != RV_OK) {
            return rc;
        }
    }

    print_findings(findings);
    const size_t errors = count_severity(findings, rv_pdklib::rv_inspect_severity::error);
    const size_t warnings = count_severity(findings, rv_pdklib::rv_inspect_severity::warning);
    rv_pdklib::rv_fprintf(stdout, k_summary_format, errors, warnings);
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
