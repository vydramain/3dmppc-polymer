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
/// @return 0 on success, 1 on refusal
int compile_sources(
    const rv_burner_options &options,
    const std::filesystem::path &binary_dir,
    std::string &error);

} // namespace rv_pdktools
