#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"

namespace rv_pdklib
{

// --- manifest glob expansion ---
//
// Every list in a manifest — build sources, assets, textures, scripts — is a
// list of glob patterns rather than file names, so a disc does not have to
// restate its own directory listing. Expanding them is the same job every time,
// so it is written once, here. The walk lives in pdklib so every tool that expands a
// manifest uses the same walk.

namespace detail
{

// Sorted listing of one directory. Sorted because a disc must burn to the same
// bytes twice in a row: readdir order is a filesystem's private business and
// letting it decide link order (or archive order) would make every rebuild a
// different file.
inline std::vector<std::filesystem::directory_entry> sorted_children(const std::filesystem::path &directory)
{
    std::vector<std::filesystem::directory_entry> children;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        children.push_back(*it);
    }
    std::sort(children.begin(),
        children.end(),
        [](const std::filesystem::directory_entry &a, const std::filesystem::directory_entry &b) {
            return a.path().filename().string() < b.path().filename().string();
        });
    return children;
}

inline void glob_descend(const std::filesystem::path &root,
    const std::filesystem::path &relative,
    const std::vector<std::string> &components,
    std::size_t index,
    std::vector<std::string> &out)
{
    const std::filesystem::path here = relative.empty() ? root : root / relative;
    if (index == components.size()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(here, ec)) {
            out.push_back(relative.generic_string());
        }
        return;
    }

    const std::string &component = components[index];

    // `**` matches zero or more directory levels.
    if (component == "**") {
        glob_descend(root, relative, components, index + 1, out);
        for (const std::filesystem::directory_entry &child : sorted_children(here)) {
            if (child.is_symlink() || !child.is_directory()) {
                continue;
            }
            glob_descend(root, relative / child.path().filename(), components, index, out);
        }
        return;
    }

    if (!has_wildcard(component)) {
        std::error_code ec;
        const std::filesystem::path next = relative / component;
        if (std::filesystem::exists(root / next, ec)) {
            glob_descend(root, next, components, index + 1, out);
        }
        return;
    }

    for (const std::filesystem::directory_entry &child : sorted_children(here)) {
        const std::string name = child.path().filename().string();
        if (!wildcard_match(component, name)) {
            continue;
        }
        if (child.is_symlink()) {
            continue;
        }
        glob_descend(root, relative / name, components, index + 1, out);
    }
}

} // namespace detail

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
inline int glob_expand(const std::filesystem::path &root,
    const std::vector<std::string> &patterns,
    std::vector<std::string> &out,
    std::string &error)
{
    out.clear();
    for (const std::string &pattern : patterns) {
        if (pattern.empty()) {
            error = "empty pattern in the manifest";
            return RV_ERR_INVAL;
        }
        if (pattern.front() == '/') {
            error = "pattern '" + pattern + "' is absolute; manifest patterns are relative to the disc directory";
            return RV_ERR_INVAL;
        }
        if (pattern.find("..") != std::string::npos) {
            error = "pattern '" + pattern + "' contains '..'; a disc may only reach files inside its own directory";
            return RV_ERR_INVAL;
        }

        std::vector<std::string> matched;
        detail::glob_descend(root, std::filesystem::path(), split_components(pattern), 0, matched);
        if (matched.empty()) {
            error = "pattern '" + pattern + "' matched no files under '" + root.string() + "'";
            return RV_ERR_INVAL;
        }
        out.insert(out.end(), matched.begin(), matched.end());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return RV_OK;
}

} // namespace rv_pdklib
