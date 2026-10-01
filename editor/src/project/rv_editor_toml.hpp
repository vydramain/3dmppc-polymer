#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rv_editor
{

// A string value for the editor's own files in disc.toml's dialect: only the
// escapes its lexer reads back.
std::string rv_editor_toml_quote(std::string_view s);

// Writes `text` to "<path>.tmp" and renames it over `path`, creating the directory
// first: a reader sees the old file or the new one, never half of one.
bool rv_editor_file_replace(const std::filesystem::path &path, const std::string &text, std::string &error);

// The whole file, or empty when it cannot be read.
std::string rv_editor_file_text(const std::filesystem::path &path);

} // namespace rv_editor
