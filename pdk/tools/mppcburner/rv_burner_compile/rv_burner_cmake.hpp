#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "rv_burner_manifest/rv_burner_manifest.hpp"
#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// --- generate a build system, do not be one ---
//
// Turning a list of .cpp files into one loadable .so by shelling out to
// `clang++ -shared` is twenty lines and works on the first disc. What it lacks
// is dependency tracking, incremental rebuilds, header scanning, parallelism and
// every per-platform detail of linking a module — each of which arrives later as
// a bug report, and answering them turns this tool into a bad build system.
//
// So the burner writes a CMakeLists.txt and drives cmake + ninja. That makes
// cmake a RUN-TIME dependency of the burner, which is a real cost, but it is one
// every C++ developer already carries. It also makes the console/disc boundary
// checkable: the generated project puts the PDK and the SDK on the include path
// and nothing else, so a disc reaching for a console header fails to compile
// instead of quietly linking against it.

/// Render the CMakeLists.txt of the generated disc project.
///
/// Pure text in, pure text out — no filesystem and no child processes — so what
/// the burner would write can be read without a disc directory.
///
/// @param manifest           the validated manifest; id, defines and sources are read
/// @param pdk_dir            PDK include directory the disc compiles against
/// @param pdklib_dir         disc-side SDK include directory
/// @param absolute_includes  extra include directories, already checked
/// @param absolute_sources   the disc's .cpp files, absolute
/// @return the complete text of a CMakeLists.txt
std::string cmake_project_text(
    const rv_burner_manifest &manifest,
    const std::string &pdk_dir,
    const std::string &pdklib_dir,
    const std::vector<std::string> &absolute_includes,
    const std::vector<std::string> &absolute_sources);

/// Write the generated project to disk: its CMakeLists.txt and the one
/// translation unit that stamps the disc version into the module.
///
/// @param options            parsed command line; `pdk_dir` and `pdklib_dir` are read
/// @param manifest           the validated manifest
/// @param project_dir        directory to create the project in; must exist
/// @param absolute_includes  extra include directories, already checked
/// @param absolute_sources   the disc's .cpp files, absolute
/// @param error              set on any write failure
/// @return 0 on success, 1 on refusal
int create_cmakelists(
    const rv_burner_options &options,
    const rv_burner_manifest &manifest,
    const std::filesystem::path &project_dir,
    const std::vector<std::string> &absolute_includes,
    const std::vector<std::string> &absolute_sources,
    std::string &error);

/// Configure the generated project with `cmake -G Ninja`, so that
/// compile_sources() has something to build.
///
/// Run once per burn, before the build. cmake and ninja are run-time
/// dependencies of mppcburner; a missing one surfaces as this step failing.
///
/// @param binary_dir   cmake binary directory to create
/// @param project_dir  directory holding the generated CMakeLists.txt
/// @param error        set with a one-line summary; the child's own output goes
///                     to stderr
/// @return 0 on success, 1 on refusal
int configure_cmake(
    const std::filesystem::path &binary_dir,
    const std::filesystem::path &project_dir,
    std::string &error);

} // namespace rv_pdktools
