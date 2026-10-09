// Format helpers of the layout file, shared by rv_editor_layout_write.cpp and rv_editor_layout_read.cpp.

#include "layout/rv_editor_layout_format.hpp"
#include "layout/rv_editor_tile.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_editor
{

using namespace layout_format;

namespace
{

constexpr std::string_view kind_names[] = { "empty",
    "catalog",
    "project",
    "files",
    "assets",
    "scene",
    "hierarchy",
    "inspector",
    "game",
    "code",
    "controls",
    "run_config",
    "output",
    "terminal",
    "problems",
    "search",
    "toolchest",
    "runtime_log",
    "build_log",
    "observe",
    "findings",
    "candidate",
    "release_controls",
    "build_result",
    "checks",
    "session",
    "test_case",
    "open_project",
    "settings",
    "help",
    "review_changes",
    "manual" };

} // namespace

namespace layout_format
{

std::string_view kind_name(rv_editor_pane_kind k)
{
    const int i = static_cast<int>(k);
    return i >= 0 && i < static_cast<int>(std::size(kind_names)) ? kind_names[i] : "";
}

int kind_from_name(std::string_view s, rv_editor_pane_kind &k)
{
    // A layout saved by an earlier editor names this pane "console"; it loads as output.
    if (s == layout_pane_legacy_console) {
        k = rv_editor_pane_kind::output;
        return RV_OK;
    }
    for (int i = 0; i < static_cast<int>(std::size(kind_names)); ++i) {
        if (kind_names[i] == s) {
            k = static_cast<rv_editor_pane_kind>(i);
            return RV_OK;
        }
    }
    return RV_ERR_INVAL;
}

int axis_from_name(std::string_view s, rv_editor_axis &a)
{
    if (s == layout_axis_x) {
        a = rv_editor_axis::x;
        return RV_OK;
    }
    if (s == layout_axis_y) {
        a = rv_editor_axis::y;
        return RV_OK;
    }
    return RV_ERR_INVAL;
}

int parse_u32(std::string_view s, uint32_t &out)
{
    if (s.empty()) {
        return RV_ERR_INVAL;
    }
    uint32_t v = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return RV_ERR_INVAL;
    }
    out = v;
    return RV_OK;
}

int parse_f32(std::string_view s, float &out)
{
    if (s.empty()) {
        return RV_ERR_INVAL;
    }
    float v = 0.0f;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return RV_ERR_INVAL;
    }
    out = v;
    return RV_OK;
}

int parse_index(std::string_view s, uint32_t &out)
{
    if (s == layout_index_marker) {
        out = rv_editor_tile_none;
        return RV_OK;
    }
    return parse_u32(s, out);
}

std::vector<std::string_view> split_line(std::string_view line)
{
    std::vector<std::string_view> parts;
    size_t p = 0;
    while (p < line.size()) {
        while (p < line.size() && line[p] == ' ') {
            ++p;
        }
        if (p >= line.size()) {
            break;
        }
        size_t q = p;
        while (q < line.size() && line[q] != ' ') {
            ++q;
        }
        parts.push_back(line.substr(p, q - p));
        p = q;
    }
    return parts;
}

std::vector<std::string_view> split_text(std::string_view text)
{
    std::vector<std::string_view> lines;
    size_t p = 0;
    while (p < text.size()) {
        size_t q = text.find('\n', p);
        if (q == std::string_view::npos) {
            if (p < text.size()) {
                lines.push_back(text.substr(p));
            }
            break;
        }
        lines.push_back(text.substr(p, q - p));
        p = q + 1;
    }
    return lines;
}

} // namespace layout_format

} // namespace rv_editor
