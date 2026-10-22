#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pdklib/rv_manifest/rv_manifest.hpp"

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
/// @return RV_OK on success; RV_ERR_INVAL for an include directory outside the disc,
///         RV_ERR_IO if one cannot be resolved
int check_sources_outside_disc(const rv_pdklib::rv_manifest &manifest,
    const std::filesystem::path &disc_dir,
    const std::vector<std::string> &sources,
    std::vector<std::string> &absolute_includes,
    std::vector<std::string> &absolute_sources,
    std::string &error);

/// Resolve the manifest's [build] sdk names into `<pdk root>/<name>/include` directories,
/// appended to @p absolute_includes. The SDK root is the parent of the contract directory
/// @p pdk_include_dir (-p or its default), so the trees are looked up where the PDK lives.
///
/// @param manifest           the validated manifest; `build_sdk` is read
/// @param pdk_include_dir    the PDK include directory (-p or its default)
/// @param absolute_includes  receives one entry per SDK tree, in manifest order
/// @param error              set with a fragment the caller prefixes
/// @return RV_OK on success; RV_ERR_NOENT if a named tree does not exist,
///         RV_ERR_IO if the PDK directory or a tree cannot be checked
int resolve_sdk_trees(const rv_pdklib::rv_manifest &manifest,
    const std::filesystem::path &pdk_include_dir,
    std::vector<std::string> &absolute_includes,
    std::string &error);

} // namespace rv_pdktools
