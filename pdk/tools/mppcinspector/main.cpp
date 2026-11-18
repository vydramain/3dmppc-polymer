// mppcinspector - check a disc directory's sources and print the findings.
//
// Parsing only: the run, its texts and its output live in rv_inspector_run. getopt prints its own
// message for an unknown option or a missing argument; the usage follows it on stderr.

#include <getopt.h>

#include <functional>
#include <string>

#include "pdk/rv_err.h"
#include "pdklib/rv_progress/rv_progress.hpp"
#include "rv_inspector_run.hpp"

namespace
{

// The exit status a script can tell apart: a disc with no error findings (warnings allowed), a disc with
// error findings, and a call that did not run (usage error or failed run).
constexpr int k_exit_clean = 0;
constexpr int k_exit_findings = 1;
constexpr int k_exit_failed = 2;

// Exactly one operand: the disc directory.
constexpr int k_operand_count = 1;

constexpr const char *k_usage_reason = "usage error";

// A refused command line is still a protocol run: the plan, the message, one failed end line. --help prints none.
int refuse_usage(const std::function<void()> &print_message)
{
    rv_pdklib::rv_progress_plan(rv_pdktools::k_inspector_tool, rv_pdktools::k_inspector_steps);
    print_message();
    rv_pdklib::rv_progress_end_failed(rv_pdktools::k_inspector_tool, k_usage_reason);
    return k_exit_failed;
}

constexpr const char *k_short_options = "hr:";

const struct option k_long_options[] = {
    { "help", no_argument, nullptr, 'h' },
    { "report", required_argument, nullptr, 'r' },
    { nullptr, 0, nullptr, 0 },
};

} // namespace

int main(int argc, char **argv)
{
    rv_pdktools::rv_inspector_options options;
    int letter = 0;
    while ((letter = getopt_long(argc, argv, k_short_options, k_long_options, nullptr)) != -1) {
        switch (letter) {
        case 'h': {
            rv_pdktools::rv_inspector_print_usage(stdout);
            return k_exit_clean;
        }
        case 'r': {
            // An empty path would silently mean no report; refuse it as a usage error.
            if (optarg[0] == '\0') {
                return refuse_usage([] {
                    rv_pdktools::rv_inspector_print_empty_report_error();
                });
            }
            options.report = optarg;
            break;
        }
        case '?':
        default: {
            return refuse_usage([] {
                rv_pdktools::rv_inspector_print_usage(stderr);
            });
        }
        }
    }

    // getopt left the operands from optind on.
    if (argc - optind != k_operand_count) {
        return refuse_usage([&] {
            rv_pdktools::rv_inspector_print_operand_error(argc - optind);
        });
    }
    options.disc = argv[optind];

    rv_pdklib::rv_progress_plan(rv_pdktools::k_inspector_tool, rv_pdktools::k_inspector_steps);
    size_t errors = 0;
    std::string error;
    if (rv_pdktools::rv_inspector_run(options, errors, error) != RV_OK) {
        rv_pdktools::rv_inspector_print_error(error);
        rv_pdklib::rv_progress_end_failed(rv_pdktools::k_inspector_tool, error);
        return k_exit_failed;
    }
    if (errors > 0) {
        rv_pdklib::rv_progress_end_failed(rv_pdktools::k_inspector_tool, std::to_string(errors) + " error(s)");
        return k_exit_findings;
    }
    rv_pdklib::rv_progress_end_ok(rv_pdktools::k_inspector_tool);
    return k_exit_clean;
}
