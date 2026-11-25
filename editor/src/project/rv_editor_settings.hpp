#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rv_editor
{

using rv_editor_settings_entries = std::vector<std::pair<std::string, std::string>>;

rv_editor_settings_entries rv_editor_settings_read_shortcuts(const std::filesystem::path &path);

// Rewrite [tools] and, when staged, [shortcuts] in one file replacement, keeping the other sections.
int rv_editor_settings_save(const std::filesystem::path &path,
    const rv_editor_settings_entries &tools,
    const std::optional<rv_editor_settings_entries> &shortcuts,
    std::string &error);

} // namespace rv_editor
