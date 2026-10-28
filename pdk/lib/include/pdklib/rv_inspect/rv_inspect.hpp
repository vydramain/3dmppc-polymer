#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"

// Source check of a disc: findings, a line-based text report and a rule table.
// Nothing here touches the disk; the program decides where the report goes.

namespace rv_pdklib
{

enum class rv_inspect_severity {
    error,
    warning,
};

struct rv_inspect_finding {
    rv_inspect_severity severity = rv_inspect_severity::error;
    // File as the rule gave it, relative to the disc directory.
    std::string file;
    // 1-based; 0 - the whole file.
    uint32_t line = 0;
    // Key or section; may be empty.
    std::string key;
    std::string rule;
    std::string text;
    // Repair rule name; empty - not repairable.
    std::string fix;
};

// Appends findings to `out`. A failure to run is not a finding: returns the error
// code and sets `error`.
using rv_inspect_check_fn = int (*)(const std::filesystem::path &disc,
    std::vector<rv_inspect_finding> &out,
    std::string &error);

struct rv_inspect_rule {
    std::string_view name;
    rv_inspect_check_fn check;
};

namespace detail
{

// Report: one finding per line, seven tab-separated fields. Tab, LF, CR and backslash
// inside a field are escaped so that a line is always exactly one finding.
// Severity words are indexed by severity.
inline constexpr std::string_view rv_inspect_severity_words[] = { "error", "warning" };
// Characters escaped in a field, and the letter that follows the backslash for each.
inline constexpr std::string_view rv_inspect_escaped = "\t\n\r\\";
inline constexpr std::string_view rv_inspect_escape_codes = "tnr\\";
// Separates fields; the eol ends every report line.
inline constexpr char rv_inspect_sep = '\t';
inline constexpr char rv_inspect_eol = '\n';
// Introduces an escape sequence.
inline constexpr char rv_inspect_escape = '\\';
// Report fields in report order; the last member is their count.
enum rv_inspect_field : size_t {
    rv_inspect_field_severity,
    rv_inspect_field_file,
    rv_inspect_field_line,
    rv_inspect_field_key,
    rv_inspect_field_rule,
    rv_inspect_field_text,
    rv_inspect_field_fix,
    rv_inspect_field_count,
};

// A text field of a report line and the finding member it fills.
struct rv_inspect_text_slot {
    size_t field;
    std::string rv_inspect_finding::*dst;
};

inline constexpr rv_inspect_text_slot rv_inspect_text_slots[] = {
    { rv_inspect_field_file, &rv_inspect_finding::file },
    { rv_inspect_field_key, &rv_inspect_finding::key },
    { rv_inspect_field_rule, &rv_inspect_finding::rule },
    { rv_inspect_field_text, &rv_inspect_finding::text },
    { rv_inspect_field_fix, &rv_inspect_finding::fix },
};

inline void rv_inspect_append_field(std::string &out, std::string_view field)
{
    for (const char c : field) {
        const size_t pos = rv_inspect_escaped.find(c);
        if (pos == std::string_view::npos) {
            out += c;
            continue;
        }
        out += rv_inspect_escape;
        out += rv_inspect_escape_codes[pos];
    }
}

inline int rv_inspect_fail(size_t line, std::string_view reason, std::string &error)
{
    error = "report line " + std::to_string(line) + ": " + std::string(reason);
    return RV_ERR_INVAL;
}

// Escapes are only \t \n \r \\; a raw CR is refused because a CRLF file would
// otherwise leave a stray CR at the end of the last field.
inline int rv_inspect_unescape(std::string_view field, std::string &out, std::string &reason)
{
    for (size_t i = 0; i < field.size(); ++i) {
        const char c = field[i];
        if (c == '\r') {
            reason = "raw CR in a field";
            return RV_ERR_INVAL;
        }
        if (c != rv_inspect_escape) {
            out += c;
            continue;
        }
        const size_t pos = (i + 1 < field.size()) ? rv_inspect_escape_codes.find(field[i + 1]) : std::string_view::npos;
        if (pos == std::string_view::npos) {
            reason = "unknown or broken escape";
            return RV_ERR_INVAL;
        }
        out += rv_inspect_escaped[pos];
        ++i;
    }
    return RV_OK;
}

inline int rv_inspect_parse_line_no(std::string_view text, uint32_t &value, std::string &reason)
{
    const bool digits = !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
    if (!digits) {
        reason = "line is not a non-negative decimal number";
        return RV_ERR_INVAL;
    }
    const char *end = text.data() + text.size();
    const std::from_chars_result res = std::from_chars(text.data(), end, value);
    if (res.ec != std::errc{}) {
        reason = "line number is out of range";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

inline int rv_inspect_parse_line(std::string_view line, rv_inspect_finding &f, std::string &reason)
{
    std::vector<std::string_view> fields;
    for (size_t start = 0;;) {
        const size_t sep = line.find(rv_inspect_sep, start);
        fields.push_back(line.substr(start, sep == std::string_view::npos ? sep : sep - start));
        if (sep == std::string_view::npos) {
            break;
        }
        start = sep + 1;
    }
    if (fields.size() != rv_inspect_field_count) {
        reason = "expected " + std::to_string(rv_inspect_field_count) + " fields, got " + std::to_string(fields.size());
        return RV_ERR_INVAL;
    }
    const auto word = std::find(std::begin(rv_inspect_severity_words),
        std::end(rv_inspect_severity_words),
        fields[rv_inspect_field_severity]);
    if (word == std::end(rv_inspect_severity_words)) {
        reason = "unknown severity";
        return RV_ERR_INVAL;
    }
    f.severity = static_cast<rv_inspect_severity>(word - std::begin(rv_inspect_severity_words));
    if (rv_inspect_parse_line_no(fields[rv_inspect_field_line], f.line, reason) != RV_OK) {
        return RV_ERR_INVAL;
    }
    for (const rv_inspect_text_slot &slot : rv_inspect_text_slots) {
        if (rv_inspect_unescape(fields[slot.field], f.*slot.dst, reason) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

} // namespace detail

inline std::string rv_inspect_report_format(std::span<const rv_inspect_finding> findings)
{
    std::string out;
    for (const rv_inspect_finding &f : findings) {
        out += detail::rv_inspect_severity_words[static_cast<size_t>(f.severity)];
        out += detail::rv_inspect_sep;
        detail::rv_inspect_append_field(out, f.file);
        out += detail::rv_inspect_sep;
        out += std::to_string(f.line);
        out += detail::rv_inspect_sep;
        detail::rv_inspect_append_field(out, f.key);
        out += detail::rv_inspect_sep;
        detail::rv_inspect_append_field(out, f.rule);
        out += detail::rv_inspect_sep;
        detail::rv_inspect_append_field(out, f.text);
        out += detail::rv_inspect_sep;
        detail::rv_inspect_append_field(out, f.fix);
        out += detail::rv_inspect_eol;
    }
    return out;
}

// On RV_OK `out` is replaced by the findings; on error it is left untouched.
inline int rv_inspect_report_parse(std::string_view text, std::vector<rv_inspect_finding> &out, std::string &error)
{
    std::vector<rv_inspect_finding> found;
    size_t pos = 0;
    size_t number = 0;
    while (pos < text.size()) {
        ++number;
        const size_t eol = text.find(detail::rv_inspect_eol, pos);
        if (eol == std::string_view::npos) {
            return detail::rv_inspect_fail(number, "no newline at the end of the report", error);
        }
        rv_inspect_finding f;
        std::string reason;
        if (detail::rv_inspect_parse_line(text.substr(pos, eol - pos), f, reason) != RV_OK) {
            return detail::rv_inspect_fail(number, reason, error);
        }
        found.push_back(std::move(f));
        pos = eol + 1;
    }
    out = std::move(found);
    return RV_OK;
}

// Runs the rules in order on one disc. Stops at the first failing rule: returns its
// code and sets `error` to "rule: reason". `out` keeps what was appended before.
inline int rv_inspect_run(std::span<const rv_inspect_rule> rules,
    const std::filesystem::path &disc,
    std::vector<rv_inspect_finding> &out,
    std::string &error)
{
    for (const rv_inspect_rule &rule : rules) {
        const int rc = rule.check(disc, out, error);
        if (rc != RV_OK) {
            error = std::string(rule.name) + ": " + error;
            return rc;
        }
    }
    return RV_OK;
}

inline bool rv_inspect_has_error(std::span<const rv_inspect_finding> findings)
{
    return std::any_of(findings.begin(), findings.end(), [](const rv_inspect_finding &f) {
        return f.severity == rv_inspect_severity::error;
    });
}

} // namespace rv_pdklib
