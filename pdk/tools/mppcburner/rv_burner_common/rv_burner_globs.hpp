#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"

namespace rv_pdktools
{

// --- manifest globs ---
//
// Every list in a manifest — build sources, assets, textures, scripts — is a
// list of glob patterns rather than file names, so a disc does not have to
// restate its own directory listing. Expanding them is the same job every time,
// so it happens here and not once per phase.
//
// The pure component matching (has_wildcard, wildcard_match, split_components)
// lives in pdklib/rv_manifest/rv_manifest_pattern.hpp, shared with the editor;
// only the filesystem walk stays here.

using rv_pdklib::has_wildcard;
using rv_pdklib::split_components;
using rv_pdklib::wildcard_match;

/// Expand manifest patterns into a sorted, duplicate-free list of files.
///
/// A pattern that matches nothing is an ERROR, not an empty result: in a
/// hand-written manifest it is a typo every time, and burning a disc without the
/// file the author listed is the worst possible reading of it. Absolute patterns
/// and `..` are refused for the same reason the include check refuses them — a
/// disc may only reach inside its own directory.
///
/// @param root      directory the patterns are resolved against
/// @param patterns  the manifest's patterns; `*`, `?` and `**` are understood
/// @param out       receives paths relative to @p root, sorted and unique
/// @param error     set when a pattern is malformed or matches no file
/// @return RV_OK on success; RV_ERR_INVAL (bad pattern or no match) leaves @p error set and @p out unusable
int glob_expand(
    const std::filesystem::path &root,
    const std::vector<std::string> &patterns,
    std::vector<std::string> &out,
    std::string &error);

} // namespace rv_pdktools
