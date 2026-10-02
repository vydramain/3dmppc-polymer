#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rv_pdklib
{

// --- manifest glob patterns ---
//
// The burner expands these patterns against a filesystem; the editor only needs
// to know whether one relative path is covered by one pattern. Both jobs share
// the same component matching, so it lives here rather than in either caller.

/// Does one path component contain a wildcard character?
///
/// @param component  a single path component, no '/' inside
/// @return true when the component contains `*` or `?`
inline bool has_wildcard(std::string_view component)
{
    return component.find('*') != std::string_view::npos ||
        component.find('?') != std::string_view::npos;
}

/// Match one path component against one wildcard pattern.
///
/// Classic backtracking match. `*` never crosses a '/' because it is only ever
/// applied within a single component.
///
/// @param pattern  the component pattern, `*` and `?` understood
/// @param text     the component to test
/// @return true when the whole component matches the whole pattern
inline bool wildcard_match(std::string_view pattern, std::string_view text)
{
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string_view::npos;
    std::size_t retry = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p;
            retry = t;
            ++p;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            ++retry;
            t = retry;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

/// Split a '/'-separated pattern into its components, dropping empty ones.
///
/// @param pattern  a manifest pattern such as `assets/**/*.png`
/// @return the components in order, here `{"assets", "**", "*.png"}`
inline std::vector<std::string> split_components(const std::string &pattern)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char c : pattern) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

namespace detail
{

// Walks pattern components against path components exactly as the burner's
// glob_descend walks the filesystem, but over an already-known path instead of
// a directory tree.
inline bool manifest_pattern_matches_from(
    const std::vector<std::string> &pattern_parts, std::size_t pi,
    const std::vector<std::string> &path_parts, std::size_t ti)
{
    if (pi == pattern_parts.size()) {
        return ti == path_parts.size();
    }

    const std::string &component = pattern_parts[pi];

    if (component == "**") {
        for (std::size_t skip = ti; skip <= path_parts.size(); ++skip) {
            if (manifest_pattern_matches_from(pattern_parts, pi + 1, path_parts, skip)) {
                return true;
            }
        }
        return false;
    }

    if (ti == path_parts.size()) {
        return false;
    }
    if (!wildcard_match(component, path_parts[ti])) {
        return false;
    }
    return manifest_pattern_matches_from(pattern_parts, pi + 1, path_parts, ti + 1);
}

} // namespace detail

/// Does a manifest pattern cover a disc-relative path?
///
/// Same semantics as the burner's glob_descend: `*`/`?` match within one
/// component, `**` spans zero or more components.
///
/// @param pattern        a manifest pattern such as `assets/**/*.png`
/// @param relative_path  a path relative to the disc root, '/'-separated
/// @return true when @p relative_path is covered by @p pattern
inline bool rv_manifest_pattern_matches(std::string_view pattern, std::string_view relative_path)
{
    const std::vector<std::string> pattern_parts = split_components(std::string(pattern));
    const std::vector<std::string> path_parts = split_components(std::string(relative_path));
    return detail::manifest_pattern_matches_from(pattern_parts, 0, path_parts, 0);
}

} // namespace rv_pdklib
