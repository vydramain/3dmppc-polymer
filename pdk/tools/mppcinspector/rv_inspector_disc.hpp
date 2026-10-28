#pragma once

// mppcinspector's disc check, kept as a header so another program can run the same check in its own process.
// It includes pdkkit, so only targets that link mppcinspector_disc may include it.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_manifest/rv_manifest_inspect.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "pdkkit/scene/rv_scene_inspect.hpp"

namespace rv_pdktools
{

namespace detail
{

// Texts of the disc check. A finding line adds its location, severity and labels around the rule's text, key,
// rule and fix, which are copied as given.
inline constexpr std::string_view rv_disc_no_dir_text = "no disc directory at ";
inline constexpr std::string_view rv_disc_not_dir_text = "not a directory: ";
inline constexpr std::string_view rv_disc_stat_text = "cannot examine disc directory ";
// Summary line: the error and warning counts.
inline constexpr const char *rv_disc_summary_format = "%zu error(s), %zu warning(s)\n";
// Bracket labels of a finding line: "... [<rule>, key <key>, fix <fix>]".
inline constexpr std::string_view rv_disc_key_label = ", key ";
inline constexpr std::string_view rv_disc_fix_label = ", fix ";

// The rule tables in run order: the disc.toml rules, then the kit's.
inline constexpr std::span<const rv_pdklib::rv_inspect_rule> rv_disc_rule_tables[] = {
    rv_pdklib::rv_manifest_inspect_rules,
    rv_pdkkit::rv_scene_inspect_rules,
};

// Absent (or a non-directory component) is RV_ERR_NOENT; any other failure to look at the path is RV_ERR_IO.
inline int rv_disc_check_dir(const std::filesystem::path &disc, std::string &error)
{
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::status(disc, ec);
    const bool missing = st.type() == std::filesystem::file_type::not_found || ec == std::errc::no_such_file_or_directory;
    const bool absent = missing || ec == std::errc::not_a_directory;
    if (ec && !absent) {
        error = std::string(rv_disc_stat_text) + disc.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    if (absent) {
        error = std::string(rv_disc_no_dir_text) + disc.string();
        return RV_ERR_NOENT;
    }
    if (!std::filesystem::is_directory(st)) {
        error = std::string(rv_disc_not_dir_text) + disc.string();
        return RV_ERR_NOENT;
    }
    return RV_OK;
}

// "<file>[:<line>]: <severity>: <text> [<rule>[, key <key>][, fix <fix>]]"; line 0 is the whole file.
inline std::string rv_disc_format_finding(const rv_pdklib::rv_inspect_finding &f)
{
    const std::string severity(rv_pdklib::detail::rv_inspect_severity_words[static_cast<size_t>(f.severity)]);
    const std::string line_no = f.line == 0 ? std::string() : ":" + std::to_string(f.line);
    const std::string key_part = f.key.empty() ? std::string() : std::string(rv_disc_key_label) + f.key;
    const std::string fix_part = f.fix.empty() ? std::string() : std::string(rv_disc_fix_label) + f.fix;
    return f.file + line_no + ": " + severity + ": " + f.text + " [" + f.rule + key_part + fix_part + "]";
}

inline size_t rv_disc_count_severity(std::span<const rv_pdklib::rv_inspect_finding> findings,
    rv_pdklib::rv_inspect_severity severity)
{
    return static_cast<size_t>(
        std::count_if(findings.begin(), findings.end(), [severity](const rv_pdklib::rv_inspect_finding &f) {
            return f.severity == severity;
        }));
}

} // namespace detail

// Replaces `findings` with the findings of `disc`: the directory is checked, then the rule tables run in order.
// A bad directory returns RV_ERR_NOENT or RV_ERR_IO with `findings` empty. A failing rule returns its code with
// `error` set and leaves in `findings` what the rules appended before the failure.
inline int rv_inspector_collect(const std::filesystem::path &disc,
    std::vector<rv_pdklib::rv_inspect_finding> &findings,
    std::string &error)
{
    findings.clear();
    if (const int rc = detail::rv_disc_check_dir(disc, error); rc != RV_OK) {
        return rc;
    }
    for (const std::span<const rv_pdklib::rv_inspect_rule> table : detail::rv_disc_rule_tables) {
        if (const int rc = rv_pdklib::rv_inspect_run(table, disc, findings, error); rc != RV_OK) {
            return rc;
        }
    }
    return RV_OK;
}

// One line per finding, to stdout.
inline void rv_inspector_print_findings(std::span<const rv_pdklib::rv_inspect_finding> findings)
{
    for (const rv_pdklib::rv_inspect_finding &f : findings) {
        rv_pdklib::rv_fprintf(stdout, "%s\n", detail::rv_disc_format_finding(f).c_str());
    }
}

// The summary line, to stdout: the error and warning counts.
inline void rv_inspector_print_summary(std::span<const rv_pdklib::rv_inspect_finding> findings)
{
    const size_t errors = detail::rv_disc_count_severity(findings, rv_pdklib::rv_inspect_severity::error);
    const size_t warnings = detail::rv_disc_count_severity(findings, rv_pdklib::rv_inspect_severity::warning);
    rv_pdklib::rv_fprintf(stdout, detail::rv_disc_summary_format, errors, warnings);
}

} // namespace rv_pdktools
