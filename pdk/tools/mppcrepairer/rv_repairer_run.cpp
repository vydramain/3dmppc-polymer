#include "rv_repairer_run.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_repair/rv_repair.hpp"
#include "pdklib/rv_repair/rv_repair_file.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "pdkkit/scene/rv_scene_repair.hpp"
#include "rv_inspector_disc.hpp"
#include "rv_inspector_report.hpp"

namespace rv_pdktools
{

namespace
{

// Titles of the two checks: the first shows what the repair works on, the second what is left after it.
constexpr const char *k_before_title = "check before repair:";
constexpr const char *k_after_title = "check after repair:";
// One line per written file: its path in the disc, then the path of its backup copy.
constexpr const char *k_written_format = "written %s, backup %s\n";
// Printed when the repair wrote nothing, so the output always says what happened.
constexpr const char *k_nothing_written_text = "nothing written";
// The usage text, printed for --help and for a usage error.
constexpr const char *k_usage_text =
    "mppcrepairer - repair a disc directory's sources by the findings of its own check\n"
    "\n"
    "Usage:\n"
    "  mppcrepairer <disc-directory> [--report PATH]\n"
    "\n"
    "The check runs first; the findings with a repair rule are repaired, and each written file and its\n"
    "backup copy are printed. Then the check runs again and its findings are printed.\n"
    "Backup copies: <$XDG_STATE_HOME or ~/.local/state>/3dmppc/repair/<absolute disc path>/\n"
    "  <path of the file in the disc>.<YYYYmmdd-HHMMSS>. An existing copy is never overwritten.\n"
    "  A relative $XDG_STATE_HOME is ignored and $HOME/.local/state is used. Without an absolute\n"
    "  $XDG_STATE_HOME or $HOME, a repair that has a file to write fails before it writes anything.\n"
    "After exit 2 a run may have written some files already: they are listed above the error,\n"
    "with their copies. A run that needs a backup copy another run took in the same second fails\n"
    "with \"backup already taken\": run it again a second later.\n"
    "\n"
    "Options:\n"
    "  -r, --report PATH  write the findings of the check after the repair to PATH, under the report\n"
    "                     contract of mppcinspector (see its --help)\n"
    "  -h, --help         print this text\n"
    "\n"
    "Exit status: 0 no error findings after the repair, 1 error findings left, 2 usage error or failed run.\n";
// A failed run's error line: the program's name, then the error.
constexpr const char *k_error_format = "mppcrepairer: %s\n";
// Reasons for a usage error that main finds before any run.
constexpr std::string_view k_operand_error_text = "expects one operand, the disc directory; got ";
constexpr std::string_view k_empty_report_error_text = "--report needs a non-empty PATH";

// Checks `disc`, replacing `findings`, and prints the title, the findings and the summary.
int check_and_print(const std::filesystem::path &disc,
    const char *title,
    std::vector<rv_pdklib::rv_inspect_finding> &findings,
    std::string &error)
{
    if (const int rc = rv_inspector_collect(disc, findings, error); rc != RV_OK) {
        return rc;
    }
    rv_pdklib::rv_fprintf(stdout, "%s\n", title);
    rv_inspector_print_findings(findings);
    rv_inspector_print_summary(findings);
    return RV_OK;
}

// One line per written file, or one line saying that none was written.
void print_written(std::span<const rv_pdklib::rv_repair_written> written)
{
    if (written.empty()) {
        rv_pdklib::rv_fprintf(stdout, "%s\n", k_nothing_written_text);
        return;
    }
    for (const rv_pdklib::rv_repair_written &w : written) {
        rv_pdklib::rv_fprintf(stdout, k_written_format, w.rel.string().c_str(), w.copy.string().c_str());
    }
}

} // namespace

int rv_repairer_run(const rv_repairer_options &options, bool &has_error, std::string &error)
{
    if (!options.report.empty()) {
        const int old_rc = rv_inspector_report_clear(options.report, error);
        if (old_rc != RV_OK) {
            return old_rc;
        }
    }

    std::vector<rv_pdklib::rv_inspect_finding> findings;
    if (const int rc = check_and_print(options.disc, k_before_title, findings, error); rc != RV_OK) {
        return rc;
    }

    std::string stamp;
    if (const int rc = rv_pdklib::rv_repair_timestamp(std::chrono::system_clock::now(), stamp, error); rc != RV_OK) {
        return rc;
    }

    // The written files are printed even when the repair failed: their backup copies are the way back.
    std::vector<rv_pdklib::rv_repair_written> written;
    const int repair_rc =
        rv_pdklib::rv_repair_apply(rv_pdkkit::rv_scene_repair_rules, options.disc, findings, stamp, written, error);
    print_written(written);
    if (repair_rc != RV_OK) {
        return repair_rc;
    }

    if (const int rc = check_and_print(options.disc, k_after_title, findings, error); rc != RV_OK) {
        return rc;
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

void rv_repairer_print_usage(std::FILE *stream)
{
    rv_pdklib::rv_fprintf(stream, "%s", k_usage_text);
}

void rv_repairer_print_error(const std::string &error)
{
    rv_pdklib::rv_fprintf(stderr, k_error_format, error.c_str());
}

void rv_repairer_print_operand_error(int got)
{
    rv_repairer_print_error(std::string(k_operand_error_text) + std::to_string(got));
    rv_repairer_print_usage(stderr);
}

void rv_repairer_print_empty_report_error()
{
    rv_repairer_print_error(std::string(k_empty_report_error_text));
    rv_repairer_print_usage(stderr);
}

} // namespace rv_pdktools
