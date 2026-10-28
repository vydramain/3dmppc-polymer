#pragma once

// Scene repair rules for rv_repair_apply: the kit's table of the fixes that scene findings name.
// A rule finds its keys in the current text, not by a finding's line: an earlier rule may have edited it.

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "pdklib/rv_repair/rv_repair.hpp"
#include "pdkkit/scene/rv_scene_inspect.hpp"
#include "pdkkit/scene/rv_scene_write.hpp"

namespace rv_pdkkit
{

namespace detail
{

// Origin the tree reader stamps on its errors; rv_repair_apply already names the file.
inline constexpr std::string_view rv_scene_repair_origin = "scene";

// Line numbers of the [scene] entries whose key is an old header.
inline int rv_scene_old_header_lines(const std::string &text, std::vector<int> &lines, std::string &error)
{
    rv_pdklib::rv_manifest_tree tree;
    if (rv_pdklib::rv_manifest_read_tree(text, std::string(rv_scene_repair_origin), tree, error) != 0) {
        return RV_ERR_INVAL;
    }
    lines.clear();
    for (const rv_pdklib::rv_manifest_tree_section &section : tree.sections) {
        // As the reader does, [[scene]] is not the header.
        if (section.name != scene_section_name || section.array) {
            continue;
        }
        for (const rv_pdklib::rv_manifest_tree_entry &e : section.entries) {
            if (rv_scene_is_old_header(e.key)) {
                lines.push_back(e.line);
            }
        }
    }
    return RV_OK;
}

// "the old header at line 2" or "the old headers at lines 2, 5", by the original line numbers.
inline std::string rv_scene_removed_at(const std::vector<int> &lines)
{
    const bool several = lines.size() > 1;
    std::string out = several ? "the old headers at lines " : "the old header at line ";
    for (size_t i = 0; i < lines.size(); ++i) {
        out += (i == 0 ? "" : ", ") + std::to_string(lines[i]);
    }
    return out;
}

// `text` without the lines numbered in `drop` (1-based), each with its newline. Other bytes are kept.
inline std::string rv_scene_drop_lines(const std::string &text, const std::vector<int> &drop)
{
    std::string out;
    size_t pos = 0;
    int number = 0;
    while (pos < text.size()) {
        const size_t end = text.find('\n', pos);
        const size_t next = end == std::string::npos ? text.size() : end + 1;
        ++number;
        if (std::find(drop.begin(), drop.end(), number) == drop.end()) {
            out.append(text, pos, next - pos);
        }
        pos = next;
    }
    return out;
}

} // namespace detail

// Removes the old header keys of [scene]. The findings are not read: the keys are found in the text.
// A text without an old header is returned unchanged, so the rule is idempotent.
inline int
rv_scene_repair_drop_header(std::string &text, std::span<const rv_pdklib::rv_inspect_finding> /*findings*/, std::string &error)
{
    std::vector<int> lines;
    if (const int rc = detail::rv_scene_old_header_lines(text, lines, error); rc != RV_OK) {
        return rc;
    }
    const std::string next = detail::rv_scene_drop_lines(text, lines);
    std::vector<int> left;
    if (const int rc = detail::rv_scene_old_header_lines(next, left, error); rc != RV_OK) {
        // The reader's line numbers count the edited text; the removed headers are named by their original lines.
        error = "removing " + detail::rv_scene_removed_at(lines) + " leaves a text the reader refuses: " + error;
        return rc;
    }
    if (!left.empty()) {
        error = "removing " + detail::rv_scene_removed_at(lines) + " leaves an old header in [scene]";
        return RV_ERR_INVAL;
    }
    text = next;
    return RV_OK;
}

// The kit's repair rules, for rv_repair_apply.
inline constexpr rv_pdklib::rv_repair_rule rv_scene_repair_rules[] = {
    { rv_scene_fix_drop_header, &rv_scene_repair_drop_header },
};

} // namespace rv_pdkkit
