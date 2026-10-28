#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"

#include "rv_manifest.hpp"
#include "detail/rv_manifest_binder.hpp"
#include "detail/rv_manifest_failer.hpp"
#include "detail/rv_manifest_lexer.hpp"
#include "detail/rv_manifest_parser.hpp"
#include "detail/rv_manifest_semantic.hpp"

// Source rules for a disc's disc.toml, run by rv_inspect_run. Each rule runs the
// stages of rv_manifest_parse itself; a stage that reports silences the later ones.

namespace rv_pdklib
{

namespace detail
{

// The manifest's name in a disc directory.
inline constexpr std::string_view rv_manifest_inspect_file = "disc.toml";

// Read buffer of rv_manifest_inspect_read, kept on the stack; a typical disc.toml fits in one read at this size.
inline constexpr std::size_t rv_manifest_inspect_chunk_size = 4096;

inline constexpr std::string_view rv_manifest_inspect_file_rule = "disc-toml-file";
inline constexpr std::string_view rv_manifest_inspect_syntax_rule = "disc-toml-syntax";
inline constexpr std::string_view rv_manifest_inspect_schema_rule = "disc-toml-schema";
inline constexpr std::string_view rv_manifest_inspect_validate_rule = "disc-toml-validate";

// The judging stages, in the order rv_manifest_parse and then rv_manifest_validate run them.
enum class rv_manifest_inspect_stage : std::size_t {
    syntax,
    schema,
    validate,
};

inline constexpr std::string_view rv_manifest_inspect_stage_rules[] = {
    rv_manifest_inspect_syntax_rule,
    rv_manifest_inspect_schema_rule,
    rv_manifest_inspect_validate_rule,
};

inline void
rv_manifest_inspect_add(std::vector<rv_inspect_finding> &out, std::string_view rule, uint32_t line, std::string text)
{
    rv_inspect_finding f;
    f.file = std::string(rv_manifest_inspect_file);
    f.line = line;
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

// Runs the stages up to `stage`. A broken earlier stage makes this one a guess, so
// nothing is reported for `stage` then.
inline void
rv_manifest_inspect_judge(const std::string &text, rv_manifest_inspect_stage stage, std::vector<rv_inspect_finding> &out)
{
    rv_manifest_failer failer;
    const std::vector<rv_manifest_token> tokens = rv_manifest_lex(text);
    rv_manifest_parser parser(tokens, failer);
    const rv_manifest_tree tree = parser.run();
    if (!failer.empty()) {
        rv_manifest_inspect_report(failer, rv_manifest_inspect_stage::syntax, stage, out);
        return;
    }
    if (stage == rv_manifest_inspect_stage::syntax) {
        return;
    }
    rv_manifest_check(tree, failer);
    if (!failer.empty()) {
        rv_manifest_inspect_report(failer, rv_manifest_inspect_stage::schema, stage, out);
        return;
    }
    if (stage == rv_manifest_inspect_stage::schema) {
        return;
    }
    const rv_manifest manifest = rv_manifest_bind(tree);
    std::string why;
    if (rv_manifest_validate(manifest, why) != RV_OK) {
        rv_manifest_inspect_add(out, rv_manifest_inspect_validate_rule, 0, why);
    }
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

// A short read is an I/O failure: a failing stream looks like the end of the data. The size compare
// assumes the reported size is the byte count, which holds for a regular file on a normal filesystem.
// A sysfs file reports 4096 and holds less, so it is refused here too.
inline int rv_manifest_inspect_read(const std::filesystem::path &file, std::string &text, std::string &error)
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    std::ifstream in(file, std::ios::binary);
    if (ec || !in) {
        error = "cannot read '" + file.string() + "'";
        return RV_ERR_IO;
    }
    std::string data;
    char chunk[rv_manifest_inspect_chunk_size];
    while (in) {
        in.read(chunk, static_cast<std::streamsize>(rv_manifest_inspect_chunk_size));
        data.append(chunk, static_cast<std::size_t>(in.gcount()));
    }
    if (in.bad() || data.size() != size) {
        error = "cannot read '" + file.string() + "'";
        return RV_ERR_IO;
    }
    text = std::move(data);
    return RV_OK;
}

// A missing manifest is the file rule's finding, so the stage rules stay silent.
inline int rv_manifest_inspect_judge_disc(const std::filesystem::path &disc,
    rv_manifest_inspect_stage stage,
    std::vector<rv_inspect_finding> &out,
    std::string &error)
{
    const std::filesystem::path file = disc / rv_manifest_inspect_file;
    bool present = false;
    if (rv_manifest_inspect_present(file, present, error) != RV_OK) {
        return RV_ERR_IO;
    }
    if (!present) {
        return RV_OK;
    }
    std::string text;
    if (rv_manifest_inspect_read(file, text, error) != RV_OK) {
        return RV_ERR_IO;
    }
    rv_manifest_inspect_judge(text, stage, out);
    return RV_OK;
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

} // namespace detail

// Rule table for rv_inspect_run, in the order the stages run.
inline constexpr rv_inspect_rule rv_manifest_inspect_rules[] = {
    { detail::rv_manifest_inspect_file_rule, &detail::rv_manifest_inspect_check_file },
    { detail::rv_manifest_inspect_syntax_rule, &detail::rv_manifest_inspect_check_syntax },
    { detail::rv_manifest_inspect_schema_rule, &detail::rv_manifest_inspect_check_schema },
    { detail::rv_manifest_inspect_validate_rule, &detail::rv_manifest_inspect_check_validate },
};

} // namespace rv_pdklib
