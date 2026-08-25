#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "rv_burner_manifest/rv_burner_manifest.hpp"

namespace rv_pdktools
{

// --- disc boundary ---
//
// A disc may include its own headers, the PDK and the SDK, and nothing else.
// The generated project enforces that by what it puts on the include path, so
// the manifest's own `include_dirs` have to be checked before they get there.

/// Resolve the manifest's sources and include directories into absolute paths,
/// refusing any include directory that leaves the disc.
///
/// Sources are only joined to @p disc_dir — glob_expand() already guaranteed
/// they are inside it. Include directories come from the manifest verbatim, so
/// each is resolved and then compared against the disc directory.
///
/// @param manifest           the validated manifest; `build_include_dirs` is read
/// @param disc_dir           absolute, canonical disc directory
/// @param sources            source paths relative to @p disc_dir
/// @param absolute_includes  receives the accepted include directories
/// @param absolute_sources   receives @p sources joined to @p disc_dir
/// @param error              set with a fragment the caller prefixes
/// @return 0 on success, 1 on refusal
int check_sources_outside_disc(
    const rv_burner_manifest &manifest,
    const std::filesystem::path &disc_dir,
    const std::vector<std::string> &sources,
    std::vector<std::string> &absolute_includes,
    std::vector<std::string> &absolute_sources,
    std::string &error);

} // namespace rv_pdktools
