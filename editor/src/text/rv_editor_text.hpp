#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rv_editor
{

// Load editor UI text strings from embedded default and optional user file.
// Sections in TOML become text IDs as "section.key"; any string value becomes an entry.
// User file must be valid TOML and overrides and adds entries.
// Returns RV_OK on success, RV_ERR_INVAL if parsing fails (with error message),
// RV_ERR_IO if user_file exists but cannot be read.
int rv_editor_text_load(const std::filesystem::path &user_file, std::string &error);

// Return text by ID (e.g., "too_small.title"). Unknown IDs return the ID itself,
// pointer stable until next rv_editor_text_load. Only called from UI thread.
const char *rv_editor_text(std::string_view id);

} // namespace rv_editor
