// mppcrepairer - repair a disc directory's sources by its own check, then check again. Parsing only: the run, its texts
// and its output live in rv_repairer_run; getopt prints its own message for a bad option, then the usage on stderr.

#include <getopt.h>

#include <string>

#include "pdk/rv_err.h"
#include "rv_repairer_run.hpp"

namespace
{

// The exit status a script can tell apart: no error findings remain after the repair (warnings allowed), error
// findings remain, and a call that did not run (usage error or failed run; a failed run may have written some files).
constexpr int k_exit_clean = 0;
constexpr int k_exit_findings = 1;
constexpr int k_exit_failed = 2;

// Exactly one operand: the disc directory.
constexpr int k_operand_count = 1;

constexpr const char *k_short_options = "hr:";

const struct option k_long_options[] = {
    { "help", no_argument, nullptr, 'h' },
    { "report", required_argument, nullptr, 'r' },
    { nullptr, 0, nullptr, 0 },
};

} // namespace

int main(int argc, char **argv)
{
    rv_pdktools::rv_repairer_options options;
    int letter = 0;
    while ((letter = getopt_long(argc, argv, k_short_options, k_long_options, nullptr)) != -1) {
        switch (letter) {
        case 'h': {
            rv_pdktools::rv_repairer_print_usage(stdout);
            return k_exit_clean;
        }
        case 'r': {
            // An empty path would silently mean no report; refuse it as a usage error.
            if (optarg[0] == '\0') {
                rv_pdktools::rv_repairer_print_empty_report_error();
                return k_exit_failed;
            }
            options.report = optarg;
            break;
        }
        case '?':
        default: {
            rv_pdktools::rv_repairer_print_usage(stderr);
            return k_exit_failed;
        }
        }
    }

    // getopt left the operands from optind on.
    if (argc - optind != k_operand_count) {
        rv_pdktools::rv_repairer_print_operand_error(argc - optind);
        return k_exit_failed;
    }
    options.disc = argv[optind];

    bool has_error = false;
    std::string error;
    if (rv_pdktools::rv_repairer_run(options, has_error, error) != RV_OK) {
        rv_pdktools::rv_repairer_print_error(error);
        return k_exit_failed;
    }
    return has_error ? k_exit_findings : k_exit_clean;
}
