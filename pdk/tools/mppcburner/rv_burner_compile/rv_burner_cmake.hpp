#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// native_module: the dlopen'd disc the console loads. web_static: a static
// archive the web kit links into the page.
enum class rv_disc_project_kind {
    native_module,
    web_static,
};

// --- generate a build system, do not be one ---
//
// Turning a list of .cpp files into one loadable .so by shelling out to
// `clang++ -shared` is twenty lines and works on the first disc (native). The web
// archive is built by cmake under the Emscripten toolchain file. What it lacks
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
/// @param project_kind       native_module or web_static
/// @return the complete text of a CMakeLists.txt
std::string cmake_project_text(const rv_pdklib::rv_manifest &manifest,
    const std::string &pdk_dir,
    const std::string &pdklib_dir,
    const std::vector<std::string> &absolute_includes,
    const std::vector<std::string> &absolute_sources,
    rv_disc_project_kind project_kind = rv_disc_project_kind::native_module);

/// File the disc target leaves in its binary directory: disc.so or libdisc.a.
///
/// @param project_kind  native_module or web_static
/// @return a static string naming the artifact
const char *disc_artifact_name(rv_disc_project_kind project_kind);

/// Write the generated project to disk: its CMakeLists.txt and the one
/// translation unit that stamps the disc version into the module.
///
/// @param options            parsed command line; `pdk_dir` and `pdklib_dir` are read
/// @param manifest           the validated manifest
/// @param project_dir        directory to create the project in; must exist
/// @param absolute_includes  extra include directories, already checked
/// @param absolute_sources   the disc's .cpp files, absolute
/// @param error              set on any write failure
/// @param project_kind       native_module (default) or web_static
/// @return RV_OK on success, RV_ERR_IO when a file cannot be written
int create_cmakelists(const rv_burner_options &options,
    const rv_pdklib::rv_manifest &manifest,
    const std::filesystem::path &project_dir,
    const std::vector<std::string> &absolute_includes,
    const std::vector<std::string> &absolute_sources,
    std::string &error,
    rv_disc_project_kind project_kind = rv_disc_project_kind::native_module);

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
/// @param toolchain_file cmake toolchain file passed as CMAKE_TOOLCHAIN_FILE; empty for none
/// @param build_type     CMAKE_BUILD_TYPE; empty means RelWithDebInfo
/// @return RV_OK on success, RV_ERR_IO when cmake fails to configure
int configure_cmake(const std::filesystem::path &binary_dir,
    const std::filesystem::path &project_dir,
    std::string &error,
    const std::filesystem::path &toolchain_file = {},
    const std::string &build_type = {});

} // namespace rv_pdktools
