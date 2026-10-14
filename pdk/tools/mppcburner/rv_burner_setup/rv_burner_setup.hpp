#pragma once

#include <filesystem>
#include <string>

#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// --- the setup subcommand ---
//
// Installs, once per user, the pinned tools a burn target needs and that are not
// part of this package. Nothing is installed system-wide.

/// Install the tool named by `options.operand` if it is not installed yet.
///
/// @param options  the parsed command line; the operand is the tool name ("web")
/// @return RV_OK when the tool is installed or was already; otherwise the RV_ERR_* of the failed step
int rv_burner_setup_run(const rv_burner_options &options);

/// Directory the web tools live in: ${XDG_DATA_HOME:-$HOME/.local/share}/3dmppc/emscripten-<version>.
/// Meant for the web burn target, which will look for its compiler here.
///
/// @param dir     receives the directory on success
/// @param reason  receives one sentence on failure
/// @return RV_OK, or RV_ERR_NOENT when neither XDG_DATA_HOME nor HOME is set
int rv_burner_emscripten_dir(std::filesystem::path &dir, std::string &reason);

} // namespace rv_pdktools
