#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rv_editor
{

// Adds `pattern` to `section`'s `files` array in a disc.toml-dialect manifest,
// editing the existing text by line instead of re-rendering it: comments,
// blank lines, order and formatting elsewhere are untouched. Already present
// is a no-op success. Written atomically; on any failure (read, parse, or the
// result failing to load back) the file is left exactly as it was. RV_OK on
// success, RV_ERR_INVAL on validation failures, code from rv_editor_file_replace on write failure.
int rv_editor_manifest_add_pattern(const std::filesystem::path &manifest,
    std::string_view section,
    std::string_view pattern,
    std::string &error);

} // namespace rv_editor
