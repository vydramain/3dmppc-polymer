#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rv_editor
{

// One [tools] key of settings.toml and its path; an empty path leaves the key out.
using rv_editor_settings_tool = std::pair<std::string, std::string>;

// `text`, a settings.toml, with its [tools] section replaced by `tools` and every
// other line kept as it was, comments included; the section is appended when missing.
std::string rv_editor_settings_with_tools(std::string_view text, const std::vector<rv_editor_settings_tool> &tools);

// Rewrites the [tools] section of the file at `path`, created when missing, through
// a temporary file and a rename (ADR 0010). False with the reason.
bool rv_editor_settings_save_tools(const std::filesystem::path &path, const std::vector<rv_editor_settings_tool> &tools,
    std::string &error);

} // namespace rv_editor
