#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rv_pdktools
{

// --- manifest globs ---
//
// Every list in a manifest — build sources, assets, textures, scripts — is a
// list of glob patterns rather than file names, so a disc does not have to
// restate its own directory listing. Expanding them is the same job every time,
// so it happens here and not once per phase.

/// Does one path component contain a wildcard character?
///
/// @param component  a single path component, no '/' inside
/// @return true when the component contains `*` or `?`
bool has_wildcard(std::string_view component);

/// Match one path component against one wildcard pattern.
///
/// Classic backtracking match. `*` never crosses a '/' because it is only ever
/// applied within a single component.
///
/// @param pattern  the component pattern, `*` and `?` understood
/// @param text     the component to test
/// @return true when the whole component matches the whole pattern
bool wildcard_match(std::string_view pattern, std::string_view text);

/// Split a '/'-separated pattern into its components, dropping empty ones.
///
/// @param pattern  a manifest pattern such as `assets/**/*.png`
/// @return the components in order, here `{"assets", "**", "*.png"}`
std::vector<std::string> split_components(const std::string &pattern);

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
/// @return true on success; false leaves @p error set and @p out unusable
bool glob_expand(
    const std::filesystem::path &root,
    const std::vector<std::string> &patterns,
    std::vector<std::string> &out,
    std::string &error);

} // namespace rv_pdktools
