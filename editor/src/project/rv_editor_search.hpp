#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rv_editor
{

// One line of a project file holding the query (TXT-06).
struct rv_editor_search_hit
{
    std::filesystem::path file; // absolute
    int32_t line = 0;
    int32_t column = 0;         // 1-based byte column of the first match
    std::string excerpt;        // the line, leading blanks cut, at most 200 bytes
};

struct rv_editor_search_result
{
    std::vector<rv_editor_search_hit> hits;
    size_t files = 0;         // text files read
    size_t skipped_files = 0; // binary or over 4 MiB
    size_t skipped_dirs = 0;  // folders left out by the default scope
    bool truncated = false;   // stopped at 2000 hits
};

// What the default scope leaves out, as the pane says it.
inline constexpr const char *rv_editor_search_skipped = "dot folders (.git, ...) and build*";

// Reads the files under `root` one line at a time, never a whole file. `all` also
// enters the folders rv_editor_search_skipped names.
rv_editor_search_result rv_editor_search_run(const std::filesystem::path &root, std::string_view query, bool match_case,
    bool all);

// The Search Results pane's state: the field and the result of the query last run.
struct rv_editor_search_view
{
    char query[256] = {};
    bool match_case = false;
    bool all = false;
    bool focus = false;   // Ctrl+Shift+F: the field takes the keyboard next frame
    std::string searched; // the query `result` is for; empty before the first run
    bool searched_all = false;
    rv_editor_search_result result;
};

} // namespace rv_editor
