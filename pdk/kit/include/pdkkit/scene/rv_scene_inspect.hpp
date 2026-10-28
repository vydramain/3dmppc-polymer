#pragma once

// The source check of one scene file, as findings for rv_inspect. Rules are the scene's own:
// the kit reads them, the tools only run them. Header-only: a disc gets it with no object code.

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "pdkkit/scene/rv_scene.hpp"

namespace rv_pdkkit
{

// Repair rules named in a finding's `fix`; the repair table registers them.
inline constexpr std::string_view rv_scene_fix_drop_header = "scene-drop-header";
inline constexpr std::string_view rv_scene_fix_form2 = "scene-form2";

namespace detail
{

inline constexpr std::string_view rv_scene_parse_rule = "scene-parse";
inline constexpr std::string_view rv_scene_old_header_rule = "scene-old-header";
inline constexpr std::string_view rv_scene_unknown_key_rule = "scene-unknown-key";
inline constexpr std::string_view rv_scene_unknown_section_rule = "scene-unknown-section";
inline constexpr std::string_view rv_scene_form1_rule = "scene-form1";

// Finding texts; the form-1 count follows its prefix.
inline constexpr std::string_view rv_scene_unknown_key_text = "unknown key";
inline constexpr std::string_view rv_scene_unknown_section_text = "unknown section";
inline constexpr std::string_view rv_scene_old_header_text = "old header: a scene has no version or format";
inline constexpr std::string_view rv_scene_form1_text = "objects in form 1: ";

// [scene] keys of the headers an old scene carried; a scene now has none.
inline constexpr std::array<std::string_view, 2> rv_scene_old_header_keys = { "version", "format" };

inline bool rv_scene_is_old_header(std::string_view key)
{
    return std::find(rv_scene_old_header_keys.begin(), rv_scene_old_header_keys.end(), key) != rv_scene_old_header_keys.end();
}

inline rv_pdklib::rv_inspect_finding &rv_scene_add(std::vector<rv_pdklib::rv_inspect_finding> &out,
    const std::string &file,
    uint32_t line,
    std::string_view rule,
    std::string_view text)
{
    rv_pdklib::rv_inspect_finding &f = out.emplace_back();
    f.file = file;
    f.line = line;
    f.rule = std::string(rule);
    f.text = std::string(text);
    return f;
}

// One unknown key of [scene] or of an object: a finding with the key's name and line.
inline void rv_scene_add_unknown(std::vector<rv_pdklib::rv_inspect_finding> &out,
    const std::string &file,
    const rv_pdklib::rv_manifest_tree_entry &e)
{
    rv_scene_add(out, file, static_cast<uint32_t>(e.line), rv_scene_unknown_key_rule, rv_scene_unknown_key_text).key = e.key;
}

// The parser writes "file:N: text" per problem; true with `number` and `text` set when `line` has that prefix.
inline bool rv_scene_take_at(std::string_view line, std::string_view file, uint32_t &number, std::string_view &text)
{
    if (!line.starts_with(file) || line.size() <= file.size() || line[file.size()] != ':') {
        return false;
    }
    const std::string_view rest = line.substr(file.size() + 1);
    const size_t sep = rest.find(": ");
    if (sep == std::string_view::npos || sep == 0) {
        return false;
    }
    const std::string_view digits = rest.substr(0, sep);
    uint32_t value = 0;
    const char *end = digits.data() + digits.size();
    const std::from_chars_result res = std::from_chars(digits.data(), end, value);
    if (res.ec != std::errc{} || res.ptr != end) {
        return false;
    }
    number = value;
    text = rest.substr(sep + 2);
    return true;
}

// The parser's error is one problem per line; each becomes a finding of the parse rule. A line without
// the prefix continues the previous finding of this call (a raw newline in its text). Known limit: a value
// with a newline and then "file:N: " text reads as a new problem, the reader embeds raw values.
inline void rv_scene_add_parse(std::vector<rv_pdklib::rv_inspect_finding> &out, const std::string &file, std::string_view error)
{
    const size_t first = out.size();
    while (!error.empty()) {
        const std::string_view line = error.substr(0, error.find('\n'));
        error.remove_prefix(std::min(error.size(), line.size() + 1));
        uint32_t number = 0;
        std::string_view text = line;
        if (rv_scene_take_at(line, file, number, text) || out.size() == first) {
            rv_scene_add(out, file, number, rv_scene_parse_rule, text);
            continue;
        }
        out.back().text += '\n';
        out.back().text.append(line);
    }
}

} // namespace detail

// Checks one scene's text; `file` is its path relative to the disc. Problems are findings and
// the result is RV_OK; only a failure to read the text is an error, with `error` set.
inline int rv_scene_inspect(const std::string &text,
    const std::string &file,
    std::vector<rv_pdklib::rv_inspect_finding> &out,
    std::string &error)
{
    rv_scene scene;
    std::string parse_error;
    if (rv_scene_parse(text, file, scene, parse_error) != 0) {
        detail::rv_scene_add_parse(out, file, parse_error);
        return RV_OK;
    }
    rv_pdklib::rv_manifest_tree tree;
    if (rv_pdklib::rv_manifest_read_tree(text, file, tree, error) != 0) {
        return RV_ERR_INVAL;
    }
    for (const rv_pdklib::rv_manifest_tree_entry &e : scene.scene_extra) {
        if (!detail::rv_scene_is_old_header(e.key)) {
            detail::rv_scene_add_unknown(out, file, e);
            continue;
        }
        rv_pdklib::rv_inspect_finding &f = detail::rv_scene_add(out,
            file,
            static_cast<uint32_t>(e.line),
            detail::rv_scene_old_header_rule,
            detail::rv_scene_old_header_text);
        f.key = e.key;
        f.fix = std::string(rv_scene_fix_drop_header);
    }
    for (const rv_pdklib::rv_manifest_tree_section &s : scene.other_sections) {
        detail::rv_scene_add(out,
            file,
            static_cast<uint32_t>(s.line),
            detail::rv_scene_unknown_section_rule,
            detail::rv_scene_unknown_section_text)
            .key = s.name;
    }
    // Component fields are not checked here: their schema is in the disc's compiled component records, not in scene text.
    for (const rv_scene_object &o : scene.objects) {
        for (const rv_pdklib::rv_manifest_tree_entry &e : o.extra) {
            detail::rv_scene_add_unknown(out, file, e);
        }
    }
    // Form 1 is one finding per file, on its first object; the count is in the text.
    const auto is_form1 = [](const rv_pdklib::rv_manifest_tree_section &s) {
        return s.name == "object" && s.array && detail::rv_scene_is_form1(s);
    };
    const auto first = std::find_if(tree.sections.begin(), tree.sections.end(), is_form1);
    if (first != tree.sections.end()) {
        const auto count = std::count_if(first, tree.sections.end(), is_form1);
        detail::rv_scene_add(out,
            file,
            static_cast<uint32_t>(first->line),
            detail::rv_scene_form1_rule,
            std::string(detail::rv_scene_form1_text) + std::to_string(count))
            .fix = std::string(rv_scene_fix_form2);
    }
    return RV_OK;
}

} // namespace rv_pdkkit
