// Project search: a line-by-line scan of the project's text files, in path order.

#include "project/rv_editor_search.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace rv_editor
{

namespace
{

constexpr size_t rv_editor_search_max_hits = 2000;
constexpr uintmax_t rv_editor_search_max_bytes = 4u << 20;
constexpr size_t rv_editor_search_excerpt = 200;
constexpr std::string_view skipped_dir_prefix = "build"; // directories skipped by project search
constexpr std::string_view leading_blanks_chars = " \t"; // whitespace to trim from excerpt

bool rv_editor_search_skips(const std::filesystem::path &dir)
{
    const std::string name = dir.filename().string();
    return name.starts_with('.') || name.starts_with(skipped_dir_prefix);
}

char rv_editor_search_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Byte offset of `query` in `line`, or npos; ASCII case folding unless `match_case`.
size_t rv_editor_search_find(std::string_view line, std::string_view query, bool match_case)
{
    if (match_case) {
        return line.find(query);
    }
    const auto it = std::search(line.begin(), line.end(), query.begin(), query.end(),
        [](char a, char b) { return rv_editor_search_lower(a) == rv_editor_search_lower(b); });
    return it == line.end() ? std::string_view::npos : static_cast<size_t>(it - line.begin());
}

// False when the file turned out binary: its hits are dropped again.
bool rv_editor_search_file(const std::filesystem::path &file, std::string_view query, bool match_case,
    std::vector<rv_editor_search_hit> &hits)
{
    std::ifstream in(file, std::ios::binary);
    const size_t first = hits.size();
    std::string line;
    int32_t number = 0;
    while (hits.size() < rv_editor_search_max_hits && std::getline(in, line)) {
        ++number;
        if (line.find('\0') != std::string::npos) {
            hits.resize(first);
            return false;
        }
        const size_t at = rv_editor_search_find(line, query, match_case);
        if (at == std::string_view::npos) {
            continue;
        }
        const size_t lead = std::min(line.find_first_not_of(leading_blanks_chars), line.size());
        std::string excerpt = line.substr(lead, rv_editor_search_excerpt);
        if (!excerpt.empty() && excerpt.back() == '\r') {
            excerpt.pop_back();
        }
        hits.push_back({ file, number, static_cast<int32_t>(at + 1), std::move(excerpt) });
    }
    return true;
}

} // namespace

rv_editor_search_result rv_editor_search_run(const std::filesystem::path &root, std::string_view query, bool match_case,
    bool all)
{
    rv_editor_search_result result;
    if (query.empty()) {
        return result;
    }
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(root,
        std::filesystem::directory_options::skip_permission_denied, ec);
    for (const auto end = std::filesystem::recursive_directory_iterator(); !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (it->is_directory(type_ec)) {
            if (!all && rv_editor_search_skips(it->path())) {
                it.disable_recursion_pending();
                ++result.skipped_dirs;
            }
            continue;
        }
        if (!it->is_regular_file(type_ec)) {
            continue;
        }
        if (it->file_size(type_ec) > rv_editor_search_max_bytes) {
            ++result.skipped_files;
            continue;
        }
        files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path &file : files) {
        if (result.hits.size() >= rv_editor_search_max_hits) {
            result.truncated = true;
            break;
        }
        if (rv_editor_search_file(file, query, match_case, result.hits)) {
            ++result.files;
        } else {
            ++result.skipped_files;
        }
    }
    return result;
}

} // namespace rv_editor
