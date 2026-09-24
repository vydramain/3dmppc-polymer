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

namespace rv_editor
{

namespace
{

constexpr std::string_view kind_names[] = {
    "empty", "catalog", "project", "files", "assets", "scene", "hierarchy", "inspector",
    "game", "code", "controls", "run_config", "output", "terminal", "problems", "search"};

std::string_view kind_name(rv_editor_pane_kind k)
{
    const int i = static_cast<int>(k);
    return i >= 0 && i < static_cast<int>(std::size(kind_names)) ? kind_names[i] : "";
}

bool kind_from_name(std::string_view s, rv_editor_pane_kind &k)
{
    for (int i = 0; i < static_cast<int>(std::size(kind_names)); ++i) {
        if (kind_names[i] == s) {
            k = static_cast<rv_editor_pane_kind>(i);
            return true;
        }
    }
    return false;
}

bool axis_from_name(std::string_view s, rv_editor_axis &a)
{
    if (s == "x") {
        a = rv_editor_axis::x;
        return true;
    }
    if (s == "y") {
        a = rv_editor_axis::y;
        return true;
    }
    return false;
}

bool parse_u32(std::string_view s, uint32_t &out)
{
    if (s.empty()) {
        return false;
    }
    uint32_t v = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return false;
    }
    out = v;
    return true;
}

bool parse_f32(std::string_view s, float &out)
{
    if (s.empty()) {
        return false;
    }
    float v = 0.0f;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return false;
    }
    out = v;
    return true;
}

bool parse_index(std::string_view s, uint32_t &out)
{
    if (s == "-") {
        out = rv_editor_tile_none;
        return true;
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
    std::string r = "3dmppc-editor-layout 1\n";
    for (const auto &p : panes.panes) {
        r += "pane " + std::string(kind_name(p.kind)) + "\n";
    }
    for (const auto &n : layout.nodes) {
        if (n.kind == rv_editor_tile_kind::free) {
            r += "node free\n";
        } else if (n.kind == rv_editor_tile_kind::leaf) {
            r += "node leaf " +
                (n.parent == rv_editor_tile_none ? std::string("-") : std::to_string(n.parent));
            r += " " + std::to_string(n.leaf.active);
            for (auto x : n.leaf.tabs) {
                r += " " + std::to_string(x);
            }
            r += "\n";
        } else {
            r += "node split " +
                (n.parent == rv_editor_tile_none ? std::string("-") : std::to_string(n.parent));
            r += (n.split.axis == rv_editor_axis::x ? " x " : " y ");
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.4f", n.split.ratio);
            r += std::string(buf) + " " + std::to_string(n.split.first) + " " +
                std::to_string(n.split.second) + "\n";
        }
    }
    r += "root " + std::to_string(layout.root) + "\n";
    r += "maximized " + (layout.maximized_leaf == rv_editor_tile_none ?
        std::string("-") : std::to_string(layout.maximized_leaf)) + "\n";
    return r;
}

bool rv_editor_layout_read(std::string_view text, rv_editor_pane_registry &panes, rv_editor_layout &layout)
{
    auto lines = split_text(text);
    size_t idx = 0;
    if (idx >= lines.size() || lines[idx] != "3dmppc-editor-layout 1") {
        return false;
    }
    ++idx;
    rv_editor_pane_registry np;
    while (idx < lines.size()) {
        auto p = split_line(lines[idx]);
        if (p.empty() || p[0] != "pane") {
            break;
        }
        if (p.size() != 2) {
            return false;
        }
        rv_editor_pane_kind k;
        if (!kind_from_name(p[1], k)) {
            return false;
        }
        np.panes.push_back({k});
        ++idx;
    }
    rv_editor_layout nl;
    while (idx < lines.size()) {
        auto p = split_line(lines[idx]);
        if (p.empty() || p[0] != "node") {
            break;
        }
        if (p.size() < 2) {
            return false;
        }
        rv_editor_tile_node n{};
        if (p[1] == "free") {
            if (p.size() != 2) {
                return false;
            }
            n.kind = rv_editor_tile_kind::free;
            n.parent = rv_editor_tile_none;
        } else if (p[1] == "leaf") {
            if (p.size() < 4) {
                return false;
            }
            n.kind = rv_editor_tile_kind::leaf;
            if (!parse_index(p[2], n.parent)) {
                return false;
            }
            if (!parse_u32(p[3], n.leaf.active)) {
                return false;
            }
            for (size_t i = 4; i < p.size(); ++i) {
                uint32_t tab_id = 0;
                if (!parse_u32(p[i], tab_id)) {
                    return false;
                }
                n.leaf.tabs.push_back(tab_id);
            }
        } else if (p[1] == "split") {
            if (p.size() != 7) {
                return false;
            }
            n.kind = rv_editor_tile_kind::split;
            if (!parse_index(p[2], n.parent)) {
                return false;
            }
            rv_editor_axis a;
            if (!axis_from_name(p[3], a)) {
                return false;
            }
            n.split.axis = a;
            if (!parse_f32(p[4], n.split.ratio)) {
                return false;
            }
            if (!parse_u32(p[5], n.split.first)) {
                return false;
            }
            if (!parse_u32(p[6], n.split.second)) {
                return false;
            }
        } else {
            return false;
        }
        nl.nodes.push_back(n);
        ++idx;
    }
    if (idx >= lines.size()) {
        return false;
    }
    auto p = split_line(lines[idx]);
    if (p.size() != 2 || p[0] != "root") {
        return false;
    }
    if (!parse_u32(p[1], nl.root)) {
        return false;
    }
    ++idx;
    if (idx >= lines.size()) {
        return false;
    }
    p = split_line(lines[idx]);
    if (p.size() != 2 || p[0] != "maximized") {
        return false;
    }
    if (!parse_index(p[1], nl.maximized_leaf)) {
        return false;
    }
    ++idx;
    if (idx != lines.size()) {
        return false;
    }
    if (nl.nodes.empty() || nl.root >= nl.nodes.size()) {
        return false;
    }
    if (nl.nodes[nl.root].kind == rv_editor_tile_kind::free ||
        nl.nodes[nl.root].parent != rv_editor_tile_none) {
        return false;
    }
    uint32_t pc = static_cast<uint32_t>(np.panes.size());
    std::vector<bool> pu(pc, false);
    for (uint32_t i = 0; i < nl.nodes.size(); ++i) {
        const auto &n = nl.nodes[i];
        if (n.kind == rv_editor_tile_kind::leaf) {
            if ((n.leaf.tabs.empty() && n.leaf.active != 0) ||
                (!n.leaf.tabs.empty() && n.leaf.active >= n.leaf.tabs.size())) {
                return false;
            }
            for (auto x : n.leaf.tabs) {
                if (x >= pc || pu[x]) {
                    return false;
                }
                pu[x] = true;
            }
        } else if (n.kind == rv_editor_tile_kind::split) {
            // The axis was read by name, so only the ratio can still be out of range.
            if (!std::isfinite(n.split.ratio) || n.split.ratio < 0.0f || n.split.ratio > 1.0f) {
                return false;
            }
            if (n.split.first == n.split.second ||
                n.split.first >= nl.nodes.size() ||
                n.split.second >= nl.nodes.size()) {
                return false;
            }
            if (nl.nodes[n.split.first].kind == rv_editor_tile_kind::free ||
                nl.nodes[n.split.second].kind == rv_editor_tile_kind::free) {
                return false;
            }
            if (nl.nodes[n.split.first].parent != i ||
                nl.nodes[n.split.second].parent != i) {
                return false;
            }
        }
    }
    for (uint32_t i = 0; i < nl.nodes.size(); ++i) {
        const auto &n = nl.nodes[i];
        if (n.kind == rv_editor_tile_kind::free || i == nl.root) {
            continue;
        }
        if (n.parent == rv_editor_tile_none || n.parent >= nl.nodes.size()) {
            return false;
        }
        const auto &pn = nl.nodes[n.parent];
        if (pn.kind != rv_editor_tile_kind::split ||
            (pn.split.first != i && pn.split.second != i)) {
            return false;
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
            return false;
        }
    }
    if (nl.maximized_leaf != rv_editor_tile_none) {
        if (nl.maximized_leaf >= nl.nodes.size() ||
            nl.nodes[nl.maximized_leaf].kind != rv_editor_tile_kind::leaf) {
            return false;
        }
    }
    panes = np;
    layout = nl;
    return true;
}

} // namespace rv_editor
