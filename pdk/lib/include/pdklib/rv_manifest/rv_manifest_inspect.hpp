#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_inspect/rv_inspect_read.hpp"

#include "rv_manifest.hpp"
#include "rv_manifest_glob.hpp"
#include "detail/rv_manifest_binder.hpp"
#include "detail/rv_manifest_failer.hpp"
#include "detail/rv_manifest_lexer.hpp"
#include "detail/rv_manifest_parser.hpp"
#include "detail/rv_manifest_schema.hpp"
#include "detail/rv_manifest_semantic.hpp"

// Source rules for a disc's disc.toml, run by rv_inspect_run. Each rule runs the
// stages of rv_manifest_parse itself; a stage that reports silences the later ones.

namespace rv_pdklib
{

namespace detail
{

// The manifest's name in a disc directory.
inline constexpr std::string_view rv_manifest_inspect_file = "disc.toml";

inline constexpr std::string_view rv_manifest_inspect_file_rule = "disc-toml-file";
inline constexpr std::string_view rv_manifest_inspect_syntax_rule = "disc-toml-syntax";
inline constexpr std::string_view rv_manifest_inspect_schema_rule = "disc-toml-schema";
inline constexpr std::string_view rv_manifest_inspect_validate_rule = "disc-toml-validate";
inline constexpr std::string_view rv_manifest_inspect_patterns_rule = "disc-toml-patterns";

// The judging stages, in the order rv_manifest_parse and then rv_manifest_validate run them,
// then the list patterns, which need a clean manifest.
enum class rv_manifest_inspect_stage : std::size_t {
    syntax,
    schema,
    validate,
    patterns,
};

inline constexpr std::string_view rv_manifest_inspect_stage_rules[] = {
    rv_manifest_inspect_syntax_rule,
    rv_manifest_inspect_schema_rule,
    rv_manifest_inspect_validate_rule,
    rv_manifest_inspect_patterns_rule,
};

inline void rv_manifest_inspect_add(std::vector<rv_inspect_finding> &out,
    std::string_view rule,
    uint32_t line,
    std::string text,
    std::string_view key = {})
{
    rv_inspect_finding f;
    f.file = std::string(rv_manifest_inspect_file);
    f.line = line;
    f.key = std::string(key);
    f.rule = std::string(rule);
    f.text = std::move(text);
    out.push_back(std::move(f));
}

// Diagnostics of the stage `found`, reported only when it is the rule's own stage.
inline void rv_manifest_inspect_report(const rv_manifest_failer &failer,
    rv_manifest_inspect_stage found,
    rv_manifest_inspect_stage stage,
    std::vector<rv_inspect_finding> &out)
{
    if (found != stage) {
        return;
    }
    for (const rv_manifest_failer::entry &e : failer.entries()) {
        rv_manifest_inspect_add(out,
            rv_manifest_inspect_stage_rules[static_cast<std::size_t>(found)],
            static_cast<uint32_t>(e.line),
            e.message);
    }
}

// The lists the burner expands with glob_expand against the disc directory.
struct rv_manifest_inspect_list {
    std::string_view section;
    std::string_view key;
    std::vector<std::string> rv_manifest::*field;
};

inline constexpr rv_manifest_inspect_list rv_manifest_inspect_lists[] = {
    { rv_manifest_section_build, rv_manifest_key_build_sources, &rv_manifest::build_sources },
    { rv_manifest_section_scripts, rv_manifest_key_scripts_sources, &rv_manifest::scripts_sources },
    { rv_manifest_section_assets, rv_manifest_key_assets_files, &rv_manifest::assets_files },
    { rv_manifest_section_assets_stream, rv_manifest_key_assets_stream_files, &rv_manifest::assets_stream_files },
};

// Line of each string of the array that `key` in `section` opens. The key's line from the tree finds its tokens.
inline std::vector<int> rv_manifest_inspect_element_lines(const rv_manifest_tree &tree,
    const std::vector<rv_manifest_token> &tokens,
    std::string_view section,
    std::string_view key)
{
    std::vector<int> lines;
    for (const rv_manifest_tree_section &s : tree.sections) {
        for (const rv_manifest_tree_entry &e : s.entries) {
            if (s.name != section || e.key != key) {
                continue;
            }
            const auto at_key = [&](const rv_manifest_token &t) {
                return t.kind == rv_manifest_token_kind::IDENT && t.line == e.line && t.text == key;
            };
            for (auto t = std::find_if(tokens.begin(), tokens.end(), at_key);
                t != tokens.end() && t->kind != rv_manifest_token_kind::RBRACKET;
                ++t) {
                if (t->kind == rv_manifest_token_kind::STRING) {
                    lines.push_back(t->line);
                }
            }
            return lines;
        }
    }
    return lines;
}

// Each pattern is expanded alone, so every refused pattern gets its own finding at its own line.
inline void rv_manifest_inspect_expand(const std::filesystem::path &disc,
    const rv_manifest_tree &tree,
    const std::vector<rv_manifest_token> &tokens,
    const rv_manifest &manifest,
    std::vector<rv_inspect_finding> &out)
{
    for (const rv_manifest_inspect_list &list : rv_manifest_inspect_lists) {
        const std::vector<std::string> &patterns = manifest.*list.field;
        const std::vector<int> lines = rv_manifest_inspect_element_lines(tree, tokens, list.section, list.key);
        const std::string key = "[" + std::string(list.section) + "] " + std::string(list.key);
        for (std::size_t i = 0; i < patterns.size(); ++i) {
            std::vector<std::string> matched;
            std::string error;
            if (glob_expand(disc, { patterns[i] }, matched, error) == RV_OK) {
                continue;
            }
            // The parser makes one string token per element, so every pattern has its line.
            const int line = i < lines.size() ? lines[i] : 0;
            rv_manifest_inspect_add(out, rv_manifest_inspect_patterns_rule, static_cast<uint32_t>(line), error, key);
        }
    }
}

// The one path through the stages. A stage that reports is reported only when it is `stage`,
// and the stages after it do not run. True when the whole chain is clean; `manifest` is then bound.
inline bool rv_manifest_inspect_judge(const std::filesystem::path &disc,
    const std::string &text,
    rv_manifest_inspect_stage stage,
    std::vector<rv_inspect_finding> &out,
    rv_manifest &manifest)
{
    rv_manifest_failer failer;
    const std::vector<rv_manifest_token> tokens = rv_manifest_lex(text);
    rv_manifest_parser parser(tokens, failer);
    const rv_manifest_tree tree = parser.run();
    if (!failer.empty()) {
        rv_manifest_inspect_report(failer, rv_manifest_inspect_stage::syntax, stage, out);
        return false;
    }
    rv_manifest_check(tree, failer);
    if (!failer.empty()) {
        rv_manifest_inspect_report(failer, rv_manifest_inspect_stage::schema, stage, out);
        return false;
    }
    manifest = rv_manifest_bind(tree);
    std::string why;
    if (rv_manifest_validate(manifest, why) != RV_OK) {
        if (stage == rv_manifest_inspect_stage::validate) {
            rv_manifest_inspect_add(out, rv_manifest_inspect_validate_rule, 0, why);
        }
        return false;
    }
    if (stage == rv_manifest_inspect_stage::patterns) {
        rv_manifest_inspect_expand(disc, tree, tokens, manifest, out);
    }
    return true;
}

// Whether the manifest is there. Not found is an answer; any other failed look is an I/O failure.
inline int rv_manifest_inspect_present(const std::filesystem::path &file, bool &present, std::string &error)
{
    std::error_code ec;
    present = std::filesystem::is_regular_file(file, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        error = "cannot look at '" + file.string() + "': " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Reads and judges disc.toml. A missing manifest is not clean; the file rule reports it.
inline int rv_manifest_inspect_load_judge(const std::filesystem::path &disc,
    rv_manifest_inspect_stage stage,
    std::vector<rv_inspect_finding> &out,
    rv_manifest &manifest,
    bool &clean,
    std::string &error)
{
    const std::filesystem::path file = disc / rv_manifest_inspect_file;
    bool present = false;
    clean = false;
    if (rv_manifest_inspect_present(file, present, error) != RV_OK) {
        return RV_ERR_IO;
    }
    if (!present) {
        return RV_OK;
    }
    std::string text;
    if (rv_inspect_read_text(file, text, error) != RV_OK) {
        return RV_ERR_IO;
    }
    clean = rv_manifest_inspect_judge(disc, text, stage, out, manifest);
    return RV_OK;
}

inline int rv_manifest_inspect_judge_disc(const std::filesystem::path &disc,
    rv_manifest_inspect_stage stage,
    std::vector<rv_inspect_finding> &out,
    std::string &error)
{
    rv_manifest manifest;
    bool clean = false;
    return rv_manifest_inspect_load_judge(disc, stage, out, manifest, clean, error);
}

inline int
rv_manifest_inspect_check_file(const std::filesystem::path &disc, std::vector<rv_inspect_finding> &out, std::string &error)
{
    bool present = false;
    if (rv_manifest_inspect_present(disc / rv_manifest_inspect_file, present, error) != RV_OK) {
        return RV_ERR_IO;
    }
    if (!present) {
        rv_manifest_inspect_add(out,
            rv_manifest_inspect_file_rule,
            0,
            std::string(rv_manifest_inspect_file) + " is missing or is not a regular file");
    }
    return RV_OK;
}

inline int
rv_manifest_inspect_check_syntax(const std::filesystem::path &disc, std::vector<rv_inspect_finding> &out, std::string &error)
{
    return rv_manifest_inspect_judge_disc(disc, rv_manifest_inspect_stage::syntax, out, error);
}

inline int
rv_manifest_inspect_check_schema(const std::filesystem::path &disc, std::vector<rv_inspect_finding> &out, std::string &error)
{
    return rv_manifest_inspect_judge_disc(disc, rv_manifest_inspect_stage::schema, out, error);
}

inline int
rv_manifest_inspect_check_validate(const std::filesystem::path &disc, std::vector<rv_inspect_finding> &out, std::string &error)
{
    return rv_manifest_inspect_judge_disc(disc, rv_manifest_inspect_stage::validate, out, error);
}

inline int
rv_manifest_inspect_check_patterns(const std::filesystem::path &disc, std::vector<rv_inspect_finding> &out, std::string &error)
{
    return rv_manifest_inspect_judge_disc(disc, rv_manifest_inspect_stage::patterns, out, error);
}

} // namespace detail

// The disc's manifest. `clean` is false when disc.toml is missing or not clean, and the disc.toml
// rules say why; `manifest` is valid only when `clean` is true. RV_ERR_IO when it cannot be read.
inline int rv_manifest_inspect_disc(const std::filesystem::path &disc, rv_manifest &manifest, bool &clean, std::string &error)
{
    std::vector<rv_inspect_finding> ignored;
    return detail::rv_manifest_inspect_load_judge(disc,
        detail::rv_manifest_inspect_stage::validate,
        ignored,
        manifest,
        clean,
        error);
}

// Rule table for rv_inspect_run, in the order the stages run.
inline constexpr rv_inspect_rule rv_manifest_inspect_rules[] = {
    { detail::rv_manifest_inspect_file_rule, &detail::rv_manifest_inspect_check_file },
    { detail::rv_manifest_inspect_syntax_rule, &detail::rv_manifest_inspect_check_syntax },
    { detail::rv_manifest_inspect_schema_rule, &detail::rv_manifest_inspect_check_schema },
    { detail::rv_manifest_inspect_validate_rule, &detail::rv_manifest_inspect_check_validate },
    { detail::rv_manifest_inspect_patterns_rule, &detail::rv_manifest_inspect_check_patterns },
};

} // namespace rv_pdklib
