#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "project/rv_editor_project.hpp"

namespace rv_editor
{

// The manifest section a disc name is looked up in; each has its own pattern list in the project.
// The order is load-bearing: each section's pattern list is indexed by its value (rv_editor_walk.cpp).
enum class rv_editor_name_section {
    assets,
    textures,
    models
};

// Every project file a section's patterns accept, not just the first, so a caller can refuse a name
// held twice. `ec` is the walk's own failure (root missing, unreadable); then `matches` is empty.
struct rv_editor_walk_result {
    std::vector<std::filesystem::path> matches;
    std::error_code ec;
};

// The files of `section` whose filename (or stem, when `by_stem`) is `name`. Answered from one index
// of the whole project, walked again once it is older than a second; a failed walk is held that long too.
rv_editor_walk_result
rv_editor_walk_name(const rv_editor_project &project, rv_editor_name_section section, bool by_stem, const std::string &name);

// Drops the index: the next rv_editor_walk_name walks the project again, so files added since are seen at once.
void rv_editor_walk_reset();

} // namespace rv_editor
