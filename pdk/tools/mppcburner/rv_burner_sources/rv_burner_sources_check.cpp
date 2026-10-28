#include "rv_burner_sources_check.hpp"

#include <filesystem>
#include <string>

#include "pdk/rv_err.h"
#include "rv_burner_assets/rv_burner_baker_path.hpp"
#include "rv_burner_common/rv_burner_process.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The inspector's program name; the lookup and the error text share it.
static constexpr const char *k_inspector = "mppcinspector";

// Exit statuses of mppcinspector (pdk/tools/mppcinspector/main.cpp): 0 no error findings,
// 1 error findings, 2 usage error or failed run (k_exit_failed). Every other status except
// -1, which is handled first, takes the branch of 2, a signal kill included: not a finding.
static constexpr int k_inspector_clean = 0;
static constexpr int k_inspector_findings = 1;

} // namespace rv_pdktools

int rv_pdktools::rv_burner_sources_check(const fs::path &disc_dir,
    const std::string &hint,
    const std::string &report,
    std::string &error)
{
    // --- find the program, as the bakers are found ---

    std::string inspector;
    const int find_err = find_baker(hint, k_inspector, inspector, error);
    if (find_err != RV_OK) {
        return find_err;
    }

    // --- run it ---

    // Options first, then `--`: a disc directory whose name starts with '-' stays an operand.
    const std::string report_arg = report.empty() ? std::string() : " --report " + shell_quote(report);
    const std::string disc_arg = shell_quote(disc_dir.string());
    const std::string command = shell_quote(inspector) + report_arg + " -- " + disc_arg;

    std::string child_output;
    const int status = run_capture(command, child_output);

    // Shown whatever the status: the findings and the fix hint are the inspector's own words.
    dump_child_output(child_output);

    if (status == -1) {
        error = "could not start '" + std::string(k_inspector) + "'";
        return RV_ERR_IO;
    }
    if (status == k_inspector_findings) {
        error = "sources have error findings (listed above), so the build does not accept them";
        return RV_ERR_INVAL;
    }
    if (status != k_inspector_clean) {
        error = "source check did not pass: '" + std::string(k_inspector) + "' ended with status " + std::to_string(status);
        return RV_ERR_IO;
    }
    return RV_OK;
}
