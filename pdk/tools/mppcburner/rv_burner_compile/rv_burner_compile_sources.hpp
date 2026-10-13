#pragma once

#include <filesystem>
#include <string>

#include "rv_burner_options.hpp"

namespace rv_pdktools
{

/// Build the generated disc project and confirm it produced disc.so.
///
/// The project must already be configured — see configure_cmake(). The child's
/// own diagnostic reaches stderr verbatim on failure, because nothing this tool
/// could say about a broken disc beats what the compiler already said. Prints
/// nothing on success: the [n/4] ladder belongs to the caller.
///
/// @param options     parsed command line; only `jobs` is read here
/// @param binary_dir  cmake binary directory to build in
/// @param error       set with this tool's one-line summary on failure
/// @return RV_OK on success; RV_ERR_IO if the build fails, RV_ERR_NOENT if it
///         produced no module, otherwise the code from stamping the checksum
int compile_sources(const rv_burner_options &options, const std::filesystem::path &binary_dir, std::string &error);

/// Run `cmake --build` on a configured project, the same line for every disc target.
///
/// @param binary_dir    cmake binary directory to build in
/// @param jobs          --parallel count; 0 leaves it to cmake
/// @param child_output  receives the child's merged output, for dump_child_output() on failure
/// @return the child's exit status, as run_capture() reports it
int build_cmake_project(const std::filesystem::path &binary_dir, int jobs, std::string &child_output);

} // namespace rv_pdktools
