#pragma once

// The source check of one scene file, as findings for rv_inspect. Rules are the scene's own:
// the kit reads them, the tools only run them. rv_scene_inspect is header-only; rv_scene_inspect_disc and
// its rule table also need the pdklib rv_manifest .cpp sources (rv_manifest_check, _bind and _validate).

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_inspect/rv_inspect_read.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "pdklib/rv_manifest/rv_manifest_glob.hpp"
#include "pdklib/rv_manifest/rv_manifest_inspect.hpp"
#include "pdkkit/scene/rv_scene.hpp"
#include "pdkkit/scene/rv_scene_migrate.hpp"

namespace rv_pdkkit
{

// Repair rules named in a finding's `fix`; the repair table registers them.
inline constexpr std::string_view rv_scene_fix_drop_header = "scene-drop-header";
inline constexpr std::string_view rv_scene_fix_form2 = "scene-form2";

namespace detail
{

// Rule names of the findings this file reports; a finding's rule says which check made it.
inline constexpr std::string_view rv_scene_parse_rule = "scene-parse";
inline constexpr std::string_view rv_scene_old_header_rule = "scene-old-header";
inline constexpr std::string_view rv_scene_unknown_key_rule = "scene-unknown-key";
inline constexpr std::string_view rv_scene_unknown_section_rule = "scene-unknown-section";
inline constexpr std::string_view rv_scene_form1_rule = "scene-form1";
// One per note the migration writes, and one for the comments its conversion loses.
inline constexpr std::string_view rv_scene_form1_note_rule = "scene-form1-note";
inline constexpr std::string_view rv_scene_form1_comments_rule = "scene-form1-comments";

// Finding texts; the form-1 count follows its prefix. A refused conversion adds the refusal and the migration's reason.
inline constexpr std::string_view rv_scene_unknown_key_text = "unknown key";
inline constexpr std::string_view rv_scene_unknown_section_text = "unknown section";
inline constexpr std::string_view rv_scene_old_header_text = "old header: a scene has no version or format";
inline constexpr std::string_view rv_scene_form1_text = "objects in form 1: ";
inline constexpr std::string_view rv_scene_form1_refused_text = ", conversion refused: ";
inline constexpr std::string_view rv_scene_form1_comments_text = "comments lost by the conversion: ";

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

inline bool rv_scene_is_object_section(const rv_pdklib::rv_manifest_tree_section &s)
{
    return s.name == detail::object_section_name && s.array;
}

inline bool rv_scene_is_form1_object(const rv_pdklib::rv_manifest_tree_section &s)
{
    return rv_scene_is_object_section(s) && rv_scene_is_form1(s);
}

// Counts the comments from `from_line` on, and the first one's line: a '#' outside a string. A string is
// double-quoted, on one line, with backslash escapes (rv_manifest_lexer); a comment runs to the line's end.
inline void rv_scene_count_comments(const std::string &text, uint32_t from_line, uint32_t &count, uint32_t &first_line)
{
    uint32_t line = 1;
    bool in_string = false;
    bool in_comment = false;
    bool escaped = false;
    for (const char c : text) {
        if (c == '\n') {
            ++line;
            in_string = false;
            in_comment = false;
            escaped = false;
            continue;
        }
        if (in_comment) {
            continue;
        }
        if (in_string) {
            const bool closes = c == '"' && !escaped;
            escaped = !escaped && c == '\\';
            in_string = !closes;
            continue;
        }
        in_string = c == '"';
        in_comment = c == '#';
        if (in_comment && line >= from_line) {
            first_line = count == 0 ? line : first_line;
            ++count;
        }
    }
}

// One warning per note the migration writes for a form-1 object, on the object's header line. The migration
// writes its notes only when it succeeds, so the caller adds these only then.
inline void rv_scene_add_notes(std::vector<rv_pdklib::rv_inspect_finding> &out,
    const std::string &file,
    const rv_pdklib::rv_manifest_tree &tree,
    const rv_scene &scene)
{
    // rv_scene_parse makes one object per [[object]] section, in file order.
    std::vector<uint32_t> lines;
    for (const rv_pdklib::rv_manifest_tree_section &s : tree.sections) {
        if (rv_scene_is_object_section(s)) {
            lines.push_back(static_cast<uint32_t>(s.line));
        }
    }
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const rv_scene_object &o = scene.objects[i];
        // The migration writes notes only for objects with a kind.
        if (o.kind.empty()) {
            continue;
        }
        std::string notes;
        migrate_object_warnings(o, notes);
        // Each note is one line, kept whole; a last line with no newline ends at the string's end.
        size_t pos = 0;
        while (pos < notes.size()) {
            const size_t end = std::min(notes.find('\n', pos), notes.size());
            rv_scene_add(out, file, lines[i], rv_scene_form1_note_rule, std::string_view(notes).substr(pos, end - pos))
                .severity = rv_pdklib::rv_inspect_severity::warning;
            pos = end + 1;
        }
    }
}

// The form-1 findings: the count on the first form-1 object, then the notes, then the comments the conversion
// loses. The migration runs on a copy; a refused one has no fix and gives its reason in the text.
inline void rv_scene_add_form1(std::vector<rv_pdklib::rv_inspect_finding> &out,
    const std::string &file,
    const std::string &text,
    const rv_pdklib::rv_manifest_tree &tree,
    const rv_scene &scene)
{
    const auto first = std::find_if(tree.sections.begin(), tree.sections.end(), rv_scene_is_form1_object);
    const auto count = std::count_if(first, tree.sections.end(), rv_scene_is_form1_object);
    rv_scene migrated = scene;
    std::string why;
    const bool refused = rv_scene_migrate(migrated, why) != RV_OK;
    // A refusal writes one line: the object and the component it clashes with.
    const std::string refusal =
        refused ? std::string(rv_scene_form1_refused_text) + why.substr(0, why.find('\n')) : std::string();
    rv_pdklib::rv_inspect_finding &f = rv_scene_add(out,
        file,
        static_cast<uint32_t>(first->line),
        rv_scene_form1_rule,
        std::string(rv_scene_form1_text) + std::to_string(count) + refusal);
    f.fix = refused ? std::string() : std::string(rv_scene_fix_form2);
    // A refused conversion writes nothing and loses nothing: no notes, no lost comments.
    if (refused) {
        return;
    }
    rv_scene_add_notes(out, file, tree, scene);
    uint32_t lost = 0;
    uint32_t lost_line = 0;
    // rv_scene_render keeps only the lines above the first section.
    rv_scene_count_comments(text, static_cast<uint32_t>(tree.sections.front().line), lost, lost_line);
    if (lost > 0) {
        rv_scene_add(out,
            file,
            lost_line,
            rv_scene_form1_comments_rule,
            std::string(rv_scene_form1_comments_text) + std::to_string(lost))
            .severity = rv_pdklib::rv_inspect_severity::warning;
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
    if (std::any_of(tree.sections.begin(), tree.sections.end(), detail::rv_scene_is_form1_object)) {
        detail::rv_scene_add_form1(out, file, text, tree, scene);
    }
    return RV_OK;
}

namespace detail
{

// The disc rule's name, prefixed to its error by rv_inspect_run.
inline constexpr std::string_view rv_scene_disc_rule = "scene-disc";
// The [build] sdk value that marks a disc built with the kit.
inline constexpr std::string_view rv_scene_disc_kit_sdk = "kit";
// The suffix that picks scene files out of the [assets] files patterns.
inline constexpr std::string_view rv_scene_disc_suffix = ".scene.toml";

// The scene files one [assets] pattern names. A refused or unmatched pattern adds nothing: the disc.toml rules report it.
inline void
rv_scene_disc_expand(const std::filesystem::path &disc, const std::string &pattern, std::vector<std::string> &scenes)
{
    std::vector<std::string> matched;
    std::string why;
    if (rv_pdklib::glob_expand(disc, { pattern }, matched, why) != RV_OK) {
        return;
    }
    for (const std::string &file : matched) {
        if (file.ends_with(rv_scene_disc_suffix)) {
            scenes.push_back(file);
        }
    }
}

} // namespace detail

// Checks every scene of a disc that builds with the kit. A disc whose disc.toml is missing or not clean, or
// whose [build] sdk does not name the kit, has no finding here: the disc.toml rules report the first.
inline int
rv_scene_inspect_disc(const std::filesystem::path &disc, std::vector<rv_pdklib::rv_inspect_finding> &out, std::string &error)
{
    rv_pdklib::rv_manifest manifest;
    bool clean = false;
    if (rv_pdklib::rv_manifest_inspect_disc(disc, manifest, clean, error) != RV_OK) {
        return RV_ERR_IO;
    }
    if (!clean) {
        return RV_OK;
    }
    const std::vector<std::string> &sdk = manifest.build_sdk;
    if (std::find(sdk.begin(), sdk.end(), detail::rv_scene_disc_kit_sdk) == sdk.end()) {
        return RV_OK;
    }
    std::vector<std::string> scenes;
    for (const std::string &pattern : manifest.assets_files) {
        detail::rv_scene_disc_expand(disc, pattern, scenes);
    }
    std::sort(scenes.begin(), scenes.end());
    scenes.erase(std::unique(scenes.begin(), scenes.end()), scenes.end());
    for (const std::string &file : scenes) {
        std::string scene_text;
        if (rv_pdklib::rv_inspect_read_text(disc / file, scene_text, error) != RV_OK) {
            return RV_ERR_IO;
        }
        const int rc = rv_scene_inspect(scene_text, file, out, error);
        if (rc != RV_OK) {
            return rc;
        }
    }
    return RV_OK;
}

// Rule table for rv_inspect_run; the kit's disc-level rule.
inline constexpr rv_pdklib::rv_inspect_rule rv_scene_inspect_rules[] = {
    { detail::rv_scene_disc_rule, &rv_scene_inspect_disc },
};

} // namespace rv_pdkkit
