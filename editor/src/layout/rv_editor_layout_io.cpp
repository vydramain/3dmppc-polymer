// Format helpers of the layout file and the tree checks of a layout, shared by rv_editor_layout_write.cpp and
// rv_editor_layout_read.cpp.

#include "layout/rv_editor_layout_format.hpp"
#include "layout/rv_editor_tile.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
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

// Tree checks below. Each refusal sets `error` with its reason. Indices passed to check_not_free and check_link
// must be in range; the callers check that first.

// `what` names the node in the reason.
int check_node_exists(const rv_editor_layout &layout, uint32_t index, std::string_view what, std::string &error)
{
    if (index >= layout.nodes.size()) {
        error = std::string(what) + " is outside the node list";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

int check_not_free(const rv_editor_layout &layout, uint32_t index, std::string_view what, std::string &error)
{
    if (layout.nodes[index].kind == rv_editor_tile_kind::free) {
        error = std::string(what) + " is a free slot";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// The parent is a split that lists the child, and the child names this parent.
int check_link(const rv_editor_layout &layout, uint32_t parent, uint32_t child, std::string &error)
{
    const auto &pn = layout.nodes[parent];
    const bool listed = pn.kind == rv_editor_tile_kind::split && (pn.split.first == child || pn.split.second == child);
    if (!listed || layout.nodes[child].parent != parent) {
        error = "parent link is broken: the parent is not a split that lists the node, or the node names another parent";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// A child of a split: a node of the list, not a free slot, and it names the split as its parent.
int check_child_node(const rv_editor_layout &layout, uint32_t split, uint32_t child, std::string &error)
{
    if (check_node_exists(layout, child, "split child", error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_not_free(layout, child, "split child", error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_link(layout, split, child, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// One leaf: its active tab is among its tabs (0 when it has none); each tab is a pane of the registry, shown once.
int check_leaf_node(const rv_editor_tile_node &n, uint32_t pane_count, std::vector<bool> &shown, std::string &error)
{
    const size_t count = n.leaf.tabs.size();
    const bool active_ok = count == 0 ? n.leaf.active == 0 : n.leaf.active < count;
    if (!active_ok) {
        error = "leaf active tab is outside its tabs";
        return RV_ERR_INVAL;
    }
    for (const rv_editor_pane_id x : n.leaf.tabs) {
        if (x >= pane_count) {
            error = "leaf shows a pane that the registry does not have";
            return RV_ERR_INVAL;
        }
        if (shown[x]) {
            error = "pane is shown in two leaves";
            return RV_ERR_INVAL;
        }
        shown[x] = true;
    }
    return RV_OK;
}

// One split: the ratio is a finite number from 0 to 1, and the two children are different nodes.
int check_split_node(const rv_editor_layout &layout, uint32_t index, std::string &error)
{
    const auto &n = layout.nodes[index];
    if (!std::isfinite(n.split.ratio) || n.split.ratio < layout_ratio_min || n.split.ratio > layout_ratio_max) {
        error = "split ratio is outside the range from 0 to 1";
        return RV_ERR_INVAL;
    }
    if (n.split.first == n.split.second) {
        error = "split children are the same node";
        return RV_ERR_INVAL;
    }
    if (check_child_node(layout, index, n.split.first, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_child_node(layout, index, n.split.second, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// The root is in the list, is not a free slot and has no parent.
int check_root(const rv_editor_layout &layout, std::string &error)
{
    if (check_node_exists(layout, layout.root, "root", error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_not_free(layout, layout.root, "root", error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (layout.nodes[layout.root].parent != rv_editor_tile_none) {
        error = "root has a parent";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Every leaf: its active tab, its tabs as panes of the registry, each pane shown in one leaf only.
int check_leaves(const rv_editor_pane_registry &panes, const rv_editor_layout &layout, std::string &error)
{
    const uint32_t pane_count = static_cast<uint32_t>(panes.panes.size());
    std::vector<bool> shown(pane_count, false);
    for (const auto &n : layout.nodes) {
        if (n.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        if (check_leaf_node(n, pane_count, shown, error) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Every split: its ratio and its children.
int check_splits(const rv_editor_layout &layout, std::string &error)
{
    for (uint32_t i = 0; i < layout.nodes.size(); ++i) {
        if (layout.nodes[i].kind != rv_editor_tile_kind::split) {
            continue;
        }
        if (check_split_node(layout, i, error) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Every node but the root and the free slots has a parent link to a split.
int check_parents(const rv_editor_layout &layout, std::string &error)
{
    for (uint32_t i = 0; i < layout.nodes.size(); ++i) {
        if (layout.nodes[i].kind == rv_editor_tile_kind::free || i == layout.root) {
            continue;
        }
        if (check_node_exists(layout, layout.nodes[i].parent, "parent", error) != RV_OK) {
            return RV_ERR_INVAL;
        }
        if (check_link(layout, layout.nodes[i].parent, i, error) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Every node that is not a free slot is reached from the root. The root and split children are in range here.
int check_reachable(const rv_editor_layout &layout, std::string &error)
{
    std::vector<bool> reached(layout.nodes.size(), false);
    std::vector<uint32_t> stack = { layout.root };
    while (!stack.empty()) {
        const uint32_t x = stack.back();
        stack.pop_back();
        if (reached[x]) {
            continue;
        }
        reached[x] = true;
        const auto &n = layout.nodes[x];
        if (n.kind == rv_editor_tile_kind::split) {
            stack.push_back(n.split.first);
            stack.push_back(n.split.second);
        }
    }
    for (uint32_t i = 0; i < layout.nodes.size(); ++i) {
        if (layout.nodes[i].kind != rv_editor_tile_kind::free && !reached[i]) {
            error = "node is not reachable from the root";
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// The maximized leaf is either none or a leaf of the list.
int check_maximized(const rv_editor_layout &layout, std::string &error)
{
    if (layout.maximized_leaf == rv_editor_tile_none) {
        return RV_OK;
    }
    if (check_node_exists(layout, layout.maximized_leaf, "maximized node", error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (layout.nodes[layout.maximized_leaf].kind != rv_editor_tile_kind::leaf) {
        error = "maximized node is not a leaf of the layout";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

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

// The six checks in their order; the first refusal is returned. A pane is shown at most once.
int check_tree(const rv_editor_pane_registry &panes, const rv_editor_layout &layout, std::string &error)
{
    if (check_root(layout, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_leaves(panes, layout, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_splits(layout, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_parents(layout, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_reachable(layout, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_maximized(layout, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

} // namespace layout_format

} // namespace rv_editor
