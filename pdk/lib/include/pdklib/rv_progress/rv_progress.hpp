// The progress protocol of the PDK tools: one line per event, each starting with the tool's name.
//
//   mppcburner: [plan] sources manifest compile assets burn
//   mppcburner: [1/5] sources ./squid-maid
//   mppcburner: [end] ok
//   mppcburner: [end] failed: reason
//
// Every tool prints through this header and the editor reads through it, so the format lives in one place.
#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "pdklib/rv_stdio/rv_stdio.hpp"

namespace rv_pdklib
{

enum class rv_progress_kind {
    plan,
    step,
    end_ok,
    end_failed,
};

// What one protocol line says; the fields a kind does not use stay empty or zero.
struct rv_progress_line {
    std::string tool;
    rv_progress_kind kind = rv_progress_kind::plan;
    std::vector<std::string> labels; // plan
    int n = 0;                       // step, 1-based
    int total = 0;                   // step
    std::string label;               // step
    std::string detail;              // step, may hold spaces, may be empty
    std::string reason;              // end_failed
};

// Printing goes to stderr unless a stream is given.
inline void rv_progress_plan(const std::string &tool, const std::vector<std::string> &labels, std::FILE *out = stderr)
{
    std::string joined;
    for (const std::string &label : labels) {
        joined += ' ' + label;
    }
    rv_fprintf(out, "%s: [plan]%s\n", tool.c_str(), joined.c_str());
}

inline void rv_progress_step(const std::string &tool,
    int n,
    int total,
    const std::string &label,
    const std::string &detail,
    std::FILE *out = stderr)
{
    rv_fprintf(out, "%s: [%d/%d] %-11s %s\n", tool.c_str(), n, total, label.c_str(), detail.c_str());
}

inline void rv_progress_end_ok(const std::string &tool, std::FILE *out = stderr)
{
    rv_fprintf(out, "%s: [end] ok\n", tool.c_str());
}

inline void rv_progress_end_failed(const std::string &tool, const std::string &reason, std::FILE *out = stderr)
{
    rv_fprintf(out, "%s: [end] failed: %s\n", tool.c_str(), reason.c_str());
}

namespace rv_progress_detail
{

// Digits only, no sign, so "-1" and "" are rejected; capped to keep the int from overflowing.
inline bool parse_number(std::string_view text, int &value)
{
    if (text.empty() || text.size() > 9) {
        return false;
    }
    value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    return true;
}

inline std::string_view skip_spaces(std::string_view text)
{
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    return text;
}

} // namespace rv_progress_detail

// True and fills `result` for a protocol line (no trailing newline); false for anything else, `result` untouched.
inline bool rv_progress_parse_line(std::string_view line, rv_progress_line &result)
{
    using namespace rv_progress_detail;
    std::size_t colon = line.find(": [");
    if (colon == 0 || colon == std::string_view::npos) {
        return false;
    }
    std::string_view tool = line.substr(0, colon);
    if (tool.find(' ') != std::string_view::npos) {
        return false;
    }
    std::string_view rest = line.substr(colon + 3);
    std::size_t close = rest.find(']');
    if (close == std::string_view::npos) {
        return false;
    }
    std::string_view tag = rest.substr(0, close);
    rest = skip_spaces(rest.substr(close + 1));

    rv_progress_line parsed;
    parsed.tool = std::string(tool);
    if (tag == "plan") {
        parsed.kind = rv_progress_kind::plan;
        while (!rest.empty()) {
            std::size_t end = rest.find(' ');
            parsed.labels.emplace_back(rest.substr(0, end));
            rest = skip_spaces(end == std::string_view::npos ? std::string_view() : rest.substr(end));
        }
    } else if (tag == "end") {
        if (rest == "ok") {
            parsed.kind = rv_progress_kind::end_ok;
        } else if (rest.substr(0, 8) == "failed: ") {
            parsed.kind = rv_progress_kind::end_failed;
            parsed.reason = std::string(rest.substr(8));
        } else {
            return false;
        }
    } else {
        std::size_t slash = tag.find('/');
        if (slash == std::string_view::npos || !parse_number(tag.substr(0, slash), parsed.n) ||
            !parse_number(tag.substr(slash + 1), parsed.total) || parsed.n < 1 || parsed.n > parsed.total) {
            return false;
        }
        parsed.kind = rv_progress_kind::step;
        std::size_t end = rest.find(' ');
        parsed.label = std::string(rest.substr(0, end));
        if (end != std::string_view::npos) {
            parsed.detail = std::string(skip_spaces(rest.substr(end)));
        }
    }
    result = std::move(parsed);
    return true;
}

} // namespace rv_pdklib
