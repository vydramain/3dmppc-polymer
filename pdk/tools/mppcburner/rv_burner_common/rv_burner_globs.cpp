#include "rv_burner_common/rv_burner_globs.hpp"

#include <algorithm>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

bool rv_pdktools::has_wildcard(std::string_view component)
{
    return component.find('*') != std::string_view::npos ||
        component.find('?') != std::string_view::npos;
}

// Classic backtracking wildcard match over one path component. `*` never
// crosses a '/' here because it is only ever applied within a component.
bool rv_pdktools::wildcard_match(std::string_view pattern, std::string_view text)
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

std::vector<std::string> rv_pdktools::split_components(const std::string &pattern)
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

namespace rv_pdktools
{

// --- directory walking ---

// Sorted listing of one directory. Sorted because a disc must burn to the same
// bytes twice in a row: readdir order is a filesystem's private business and
// letting it decide link order (or archive order) would make every rebuild a
// different file.
static std::vector<fs::directory_entry> sorted_children(const fs::path &directory)
{
    std::vector<fs::directory_entry> children;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        children.push_back(*it);
    }
    std::sort(children.begin(), children.end(),
        [](const fs::directory_entry &a, const fs::directory_entry &b) {
            return a.path().filename().string() < b.path().filename().string();
        });
    return children;
}

static void glob_descend(const fs::path &root, const fs::path &relative,
    const std::vector<std::string> &components, std::size_t index,
    std::vector<std::string> &out)
{
    const fs::path here = relative.empty() ? root : root / relative;
    if (index == components.size()) {
        std::error_code ec;
        if (fs::is_regular_file(here, ec)) {
            out.push_back(relative.generic_string());
        }
        return;
    }

    const std::string &component = components[index];

    // `**` matches zero or more directory levels.
    if (component == "**") {
        glob_descend(root, relative, components, index + 1, out);
        for (const fs::directory_entry &child : sorted_children(here)) {
            if (child.is_symlink() || !child.is_directory()) {
                continue;
            }
            glob_descend(root, relative / child.path().filename(), components, index, out);
        }
        return;
    }

    if (!has_wildcard(component)) {
        std::error_code ec;
        const fs::path next = relative / component;
        if (fs::exists(root / next, ec)) {
            glob_descend(root, next, components, index + 1, out);
        }
        return;
    }

    for (const fs::directory_entry &child : sorted_children(here)) {
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

} // namespace rv_pdktools

bool rv_pdktools::glob_expand(const fs::path &root, const std::vector<std::string> &patterns,
    std::vector<std::string> &out, std::string &error)
{
    out.clear();
    for (const std::string &pattern : patterns) {
        if (pattern.empty()) {
            error = "empty pattern in the manifest";
            return false;
        }
        if (pattern.front() == '/') {
            error = "pattern '" + pattern +
                "' is absolute; manifest patterns are relative to the disc directory";
            return false;
        }
        if (pattern.find("..") != std::string::npos) {
            error = "pattern '" + pattern +
                "' contains '..'; a disc may only reach files inside its own directory";
            return false;
        }

        std::vector<std::string> matched;
        glob_descend(root, fs::path(), split_components(pattern), 0, matched);
        if (matched.empty()) {
            error = "pattern '" + pattern + "' matched no files under '" + root.string() + "'";
            return false;
        }
        out.insert(out.end(), matched.begin(), matched.end());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}
