#include "rv_burner_archive.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>

namespace fs = std::filesystem;

std::string rv_pdktools::flat_name(const std::string &relative_path)
{
    return fs::path(relative_path).filename().string();
}

bool rv_pdktools::check_asset_name(const std::string &name, std::string &error)
{
    if (name.empty()) {
        error = "an asset resolved to an empty name";
        return false;
    }

    // Belt and braces: flat_name() already strips directories, but a name
    // arriving from anywhere else must still meet rv_cd's contract.
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        error = "asset name '" + name + "' contains a path separator; rv_cd forbids them";
        return false;
    }

    if (name[0] == '.') {
        error = "asset name '" + name +
            "' starts with a dot; dotfiles are editor and VCS bookkeeping, not disc content";
        return false;
    }

    if (name == "disc.toml" || name == "disc.so") {
        error = "asset name '" + name +
            "' collides with a service entry; the loader reads that name before it looks at "
            "the asset namespace";
        return false;
    }

    return true;
}

bool rv_pdktools::check_collisions(const std::vector<archive_item> &items, std::string &error)
{
    // Sorted on a copy, so equal names land next to each other and one pass
    // finds them. The caller's order is the archive's order and is left alone.
    std::vector<archive_item> sorted = items;
    std::sort(sorted.begin(), sorted.end(),
        [](const archive_item &a, const archive_item &b) {
            return a.name < b.name;
        });

    for (std::size_t i = 1; i < sorted.size(); ++i) {
        if (sorted[i].name == sorted[i - 1].name) {
            error = "two files collapse onto the archive name '" + sorted[i].name + "': '" +
                sorted[i - 1].source + "' and '" + sorted[i].source +
                "'. The disc namespace is flat — rename one of them.";
            return false;
        }
    }

    return true;
}
