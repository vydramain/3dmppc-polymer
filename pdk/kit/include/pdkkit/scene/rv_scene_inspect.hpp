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

namespace rv_pdkkit
{

namespace detail
{

// Rule names of the findings this file reports; a finding's rule says which check made it.
inline constexpr std::string_view rv_scene_parse_rule = "scene-parse";
inline constexpr std::string_view rv_scene_old_header_rule = "scene-old-header";
inline constexpr std::string_view rv_scene_unknown_key_rule = "scene-unknown-key";
inline constexpr std::string_view rv_scene_unknown_section_rule = "scene-unknown-section";

// Finding texts.
inline constexpr std::string_view rv_scene_unknown_key_text = "unknown key";
inline constexpr std::string_view rv_scene_unknown_section_text = "unknown section";
inline constexpr std::string_view rv_scene_old_header_text = "old header: a scene has no version or format; delete this line";

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

// Checks one scene's text; `file` is its path relative to the disc. Every problem is a finding.
inline void rv_scene_inspect(const std::string &text, const std::string &file, std::vector<rv_pdklib::rv_inspect_finding> &out)
{
    rv_scene scene;
    std::string parse_error;
    if (rv_scene_parse(text, file, scene, parse_error) != 0) {
        detail::rv_scene_add_parse(out, file, parse_error);
        return;
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
        rv_scene_inspect(scene_text, file, out);
    }
    return RV_OK;
}

// Rule table for rv_inspect_run; the kit's disc-level rule.
inline constexpr rv_pdklib::rv_inspect_rule rv_scene_inspect_rules[] = {
    { detail::rv_scene_disc_rule, &rv_scene_inspect_disc },
};

} // namespace rv_pdkkit
