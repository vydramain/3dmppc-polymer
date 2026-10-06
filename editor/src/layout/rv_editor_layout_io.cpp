// Text form of the workspace: rv_editor_layout_write and rv_editor_layout_read.

#include "layout/rv_editor_tile.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_version/rv_version.hpp"

namespace rv_editor
{

namespace
{

// Layout file format syntax and keywords.
constexpr std::string_view layout_header_prefix = "3dmppc-editor-layout ";
constexpr std::string_view layout_field_sep = " ";
constexpr std::string_view layout_line_end = "\n";
constexpr std::string_view layout_keyword_pane = "pane";
constexpr std::string_view layout_keyword_node = "node";
constexpr std::string_view layout_keyword_root = "root";
constexpr std::string_view layout_keyword_maximized = "maximized";
constexpr std::string_view layout_node_type_free = "free";
constexpr std::string_view layout_node_type_leaf = "leaf";
constexpr std::string_view layout_node_type_split = "split";
constexpr std::string_view layout_axis_x = "x";
constexpr std::string_view layout_axis_y = "y";
constexpr std::string_view layout_index_marker = "-";
constexpr std::string_view layout_pane_legacy_console = "console";
// Protocol field counts for file format.
constexpr int layout_pane_field_count = 2;
constexpr int layout_leaf_field_min = 4;
constexpr int layout_node_field_min = 2;
constexpr int layout_split_field_count = 7;
// Buffer size for formatting split ratio with %.4f
constexpr int layout_ratio_format_buf_size = 16;
// Ratio range bounds: valid split division is from 0% to 100%
constexpr float layout_ratio_min = 0.0f;
constexpr float layout_ratio_max = 1.0f;

const std::string rv_editor_layout_header = std::string(layout_header_prefix) + rv_pdklib::rv_version_str;

constexpr std::string_view kind_names[] = {
    "empty", "catalog", "project", "files", "assets", "scene", "hierarchy", "inspector",
    "game", "code", "controls", "run_config", "output", "terminal", "problems", "search",
    "toolchest", "runtime_log", "build_log", "observe", "findings", "candidate", "release_controls", "build_result",
    "checks", "session", "test_case", "open_project", "settings", "help", "review_changes", "manual"
};

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

} // namespace

std::string rv_editor_layout_write(const rv_editor_pane_registry &panes, const rv_editor_layout &layout)
{
    // Only the panes the tree still shows are written, numbered in tree order: a
    // closed pane stays in the registry for the session, never in the file.
    std::vector<uint32_t> renumber(panes.panes.size(), rv_editor_tile_none);
    std::string pane_lines;
    uint32_t written = 0;
    for (const auto &n : layout.nodes) {
        if (n.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id x : n.leaf.tabs) {
            renumber[x] = written++;
            pane_lines += std::string(layout_keyword_pane) + std::string(layout_field_sep) +
                std::string(kind_name(panes.panes[x].kind)) + std::string(layout_line_end);
        }
    }

    std::string r = rv_editor_layout_header + std::string(layout_line_end) + pane_lines;
    for (const auto &n : layout.nodes) {
        if (n.kind == rv_editor_tile_kind::free) {
            r += std::string(layout_keyword_node) + std::string(layout_field_sep) +
                std::string(layout_node_type_free) + std::string(layout_line_end);
        } else if (n.kind == rv_editor_tile_kind::leaf) {
            std::string parent_str;
            if (n.parent == rv_editor_tile_none) {
                parent_str = layout_index_marker;
            } else {
                parent_str = std::to_string(n.parent);
            }
            r += std::string(layout_keyword_node) + std::string(layout_field_sep) +
                std::string(layout_node_type_leaf) + std::string(layout_field_sep) + parent_str;
            r += std::string(layout_field_sep) + std::to_string(n.leaf.active);
            for (auto x : n.leaf.tabs) {
                r += std::string(layout_field_sep) + std::to_string(renumber[x]);
            }
            r += std::string(layout_line_end);
        } else {
            std::string parent_str;
            if (n.parent == rv_editor_tile_none) {
                parent_str = layout_index_marker;
            } else {
                parent_str = std::to_string(n.parent);
            }
            std::string axis_str;
            if (n.split.axis == rv_editor_axis::x) {
                axis_str = std::string(layout_axis_x);
            } else {
                axis_str = std::string(layout_axis_y);
            }
            r += std::string(layout_keyword_node) + std::string(layout_field_sep) +
                std::string(layout_node_type_split) + std::string(layout_field_sep) + parent_str;
            r += std::string(layout_field_sep) + axis_str;
            char buf[layout_ratio_format_buf_size];
            std::snprintf(buf, sizeof(buf), "%.4f", n.split.ratio);
            r += std::string(layout_field_sep) + std::string(buf) +
                std::string(layout_field_sep) + std::to_string(n.split.first) +
                std::string(layout_field_sep) + std::to_string(n.split.second) +
                std::string(layout_line_end);
        }
    }
    r += std::string(layout_keyword_root) + std::string(layout_field_sep) +
        std::to_string(layout.root) + std::string(layout_line_end);
    std::string max_leaf_str;
    if (layout.maximized_leaf == rv_editor_tile_none) {
        max_leaf_str = layout_index_marker;
    } else {
        max_leaf_str = std::to_string(layout.maximized_leaf);
    }
    r += std::string(layout_keyword_maximized) + std::string(layout_field_sep) + max_leaf_str +
        std::string(layout_line_end);
    return r;
}

int rv_editor_layout_read(std::string_view text, rv_editor_pane_registry &panes, rv_editor_layout &layout)
{
    auto lines = split_text(text);
    size_t idx = 0;
    if (idx >= lines.size() || lines[idx] != rv_editor_layout_header) {
        return RV_ERR_INVAL;
    }
    ++idx;
    rv_editor_pane_registry np;
    while (idx < lines.size()) {
        auto p = split_line(lines[idx]);
        if (p.empty() || p[0] != layout_keyword_pane) {
            break;
        }
        if (p.size() != layout_pane_field_count) {
            return RV_ERR_INVAL;
        }
        rv_editor_pane_kind k;
        if (kind_from_name(p[1], k) != RV_OK) {
            return RV_ERR_INVAL;
        }
        np.panes.push_back({k});
        ++idx;
    }
    rv_editor_layout nl;
    while (idx < lines.size()) {
        auto p = split_line(lines[idx]);
        if (p.empty() || p[0] != layout_keyword_node) {
            break;
        }
        if (p.size() < layout_node_field_min) {
            return RV_ERR_INVAL;
        }
        rv_editor_tile_node n{};
        if (p[1] == layout_node_type_free) {
            if (p.size() != layout_pane_field_count) {
                return RV_ERR_INVAL;
            }
            n.kind = rv_editor_tile_kind::free;
            n.parent = rv_editor_tile_none;
        } else if (p[1] == layout_node_type_leaf) {
            if (p.size() < layout_leaf_field_min) {
                return RV_ERR_INVAL;
            }
            n.kind = rv_editor_tile_kind::leaf;
            if (parse_index(p[2], n.parent) != RV_OK) {
                return RV_ERR_INVAL;
            }
            if (parse_u32(p[3], n.leaf.active) != RV_OK) {
                return RV_ERR_INVAL;
            }
            for (size_t i = layout_leaf_field_min; i < p.size(); ++i) {
                uint32_t tab_id = 0;
                if (parse_u32(p[i], tab_id) != RV_OK) {
                    return RV_ERR_INVAL;
                }
                n.leaf.tabs.push_back(tab_id);
            }
        } else if (p[1] == layout_node_type_split) {
            if (p.size() != layout_split_field_count) {
                return RV_ERR_INVAL;
            }
            n.kind = rv_editor_tile_kind::split;
            if (parse_index(p[2], n.parent) != RV_OK) {
                return RV_ERR_INVAL;
            }
            rv_editor_axis a;
            if (axis_from_name(p[3], a) != RV_OK) {
                return RV_ERR_INVAL;
            }
            n.split.axis = a;
            if (parse_f32(p[4], n.split.ratio) != RV_OK) {
                return RV_ERR_INVAL;
            }
            if (parse_u32(p[5], n.split.first) != RV_OK) {
                return RV_ERR_INVAL;
            }
            if (parse_u32(p[6], n.split.second) != RV_OK) {
                return RV_ERR_INVAL;
            }
        } else {
            return RV_ERR_INVAL;
        }
        nl.nodes.push_back(n);
        ++idx;
    }
    if (idx >= lines.size()) {
        return RV_ERR_INVAL;
    }
    auto p = split_line(lines[idx]);
    if (p.size() != layout_pane_field_count || p[0] != layout_keyword_root) {
        return RV_ERR_INVAL;
    }
    if (parse_u32(p[1], nl.root) != RV_OK) {
        return RV_ERR_INVAL;
    }
    ++idx;
    if (idx >= lines.size()) {
        return RV_ERR_INVAL;
    }
    p = split_line(lines[idx]);
    if (p.size() != layout_pane_field_count || p[0] != layout_keyword_maximized) {
        return RV_ERR_INVAL;
    }
    if (parse_index(p[1], nl.maximized_leaf) != RV_OK) {
        return RV_ERR_INVAL;
    }
    ++idx;
    if (idx != lines.size()) {
        return RV_ERR_INVAL;
    }
    if (nl.nodes.empty() || nl.root >= nl.nodes.size()) {
        return RV_ERR_INVAL;
    }
    if (nl.nodes[nl.root].kind == rv_editor_tile_kind::free ||
        nl.nodes[nl.root].parent != rv_editor_tile_none) {
        return RV_ERR_INVAL;
    }
    uint32_t pc = static_cast<uint32_t>(np.panes.size());
    std::vector<bool> pu(pc, false);
    for (uint32_t i = 0; i < nl.nodes.size(); ++i) {
        const auto &n = nl.nodes[i];
        if (n.kind == rv_editor_tile_kind::leaf) {
            if ((n.leaf.tabs.empty() && n.leaf.active != 0) ||
                (!n.leaf.tabs.empty() && n.leaf.active >= n.leaf.tabs.size())) {
                return RV_ERR_INVAL;
            }
            for (auto x : n.leaf.tabs) {
                if (x >= pc || pu[x]) {
                    return RV_ERR_INVAL;
                }
                pu[x] = true;
            }
        } else if (n.kind == rv_editor_tile_kind::split) {
            // The axis was read by name, so only the ratio can still be out of range.
            if (!std::isfinite(n.split.ratio) || n.split.ratio < layout_ratio_min ||
                n.split.ratio > layout_ratio_max) {
                return RV_ERR_INVAL;
            }
            if (n.split.first == n.split.second ||
                n.split.first >= nl.nodes.size() ||
                n.split.second >= nl.nodes.size()) {
                return RV_ERR_INVAL;
            }
            if (nl.nodes[n.split.first].kind == rv_editor_tile_kind::free ||
                nl.nodes[n.split.second].kind == rv_editor_tile_kind::free) {
                return RV_ERR_INVAL;
            }
            if (nl.nodes[n.split.first].parent != i ||
                nl.nodes[n.split.second].parent != i) {
                return RV_ERR_INVAL;
            }
        }
    }
    for (uint32_t i = 0; i < nl.nodes.size(); ++i) {
        const auto &n = nl.nodes[i];
        if (n.kind == rv_editor_tile_kind::free || i == nl.root) {
            continue;
        }
        if (n.parent == rv_editor_tile_none || n.parent >= nl.nodes.size()) {
            return RV_ERR_INVAL;
        }
        const auto &pn = nl.nodes[n.parent];
        if (pn.kind != rv_editor_tile_kind::split ||
            (pn.split.first != i && pn.split.second != i)) {
            return RV_ERR_INVAL;
        }
    }
    std::vector<bool> r(nl.nodes.size(), false);
    std::vector<uint32_t> s;
    s.push_back(nl.root);
    while (!s.empty()) {
        uint32_t x = s.back();
        s.pop_back();
        if (r[x]) {
            continue;
        }
        r[x] = true;
        const auto &n = nl.nodes[x];
        if (n.kind == rv_editor_tile_kind::split) {
            s.push_back(n.split.first);
            s.push_back(n.split.second);
        }
    }
    for (uint32_t i = 0; i < nl.nodes.size(); ++i) {
        if (nl.nodes[i].kind != rv_editor_tile_kind::free && !r[i]) {
            return RV_ERR_INVAL;
        }
    }
    if (nl.maximized_leaf != rv_editor_tile_none) {
        if (nl.maximized_leaf >= nl.nodes.size() ||
            nl.nodes[nl.maximized_leaf].kind != rv_editor_tile_kind::leaf) {
            return RV_ERR_INVAL;
        }
    }
    panes = np;
    layout = nl;
    return RV_OK;
}

} // namespace rv_editor
