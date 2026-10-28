#pragma once

#include <filesystem>
#include <string>

namespace rv_pdktools
{

// --- the sources check ---
//
// Runs mppcinspector on a disc directory before anything is compiled, so sources
// with error findings never reach the build. Its output is shown unchanged once
// the inspector has finished; this module does not restate its findings.

/// Check a disc directory's sources with mppcinspector.
///
/// @param disc_dir  the disc directory handed to mppcinspector
/// @param hint      value of --baker: the directory, or a file in it, where mppcinspector
///                  is looked for; empty searches next to the burner, then $PATH
/// @param report    a --report path, or empty for no report
/// @param error     set on every failure, naming the reason
/// @return RV_OK when the sources have no error findings; RV_ERR_NOENT when mppcinspector
/// is not found; RV_ERR_INVAL when the hint cannot be used (the error names it) or
/// mppcinspector reports error findings (listed above); RV_ERR_IO when it cannot be
/// started or does not complete a check
int rv_burner_sources_check(const std::filesystem::path &disc_dir,
    const std::string &hint,
    const std::string &report,
    std::string &error);

} // namespace rv_pdktools
