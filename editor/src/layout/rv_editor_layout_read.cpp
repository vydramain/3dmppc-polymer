// Text form of the workspace, read side: rv_editor_layout_read. The write side is in rv_editor_layout_write.cpp,
// the format helpers both sides use are in rv_editor_layout_io.cpp.

#include "layout/rv_editor_layout_format.hpp"
#include "layout/rv_editor_tile.hpp"

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

// The first line must be the header of this build.
int read_header(const std::vector<std::string_view> &lines, size_t &idx, std::string &error)
{
    if (idx >= lines.size() || lines[idx] != rv_editor_layout_header) {
        error = "the first line is not the layout header of this build";
        return RV_ERR_INVAL;
    }
    ++idx;
    return RV_OK;
}

// Pane lines up to the first line that is not one. A pane keeps its kind; its id is its line order.
int read_panes(const std::vector<std::string_view> &lines, size_t &idx, rv_editor_pane_registry &np, std::string &error)
{
    while (idx < lines.size()) {
        auto p = split_line(lines[idx]);
        if (p.empty() || p[0] != layout_keyword_pane) {
            break;
        }
        if (p.size() != layout_pane_field_count) {
            error = "pane line has a wrong field count";
            return RV_ERR_INVAL;
        }
        rv_editor_pane_kind k;
        if (kind_from_name(p[1], k) != RV_OK) {
            error = "pane kind is unknown";
            return RV_ERR_INVAL;
        }
        np.panes.push_back({ k });
        ++idx;
    }
    return RV_OK;
}

// Each reader parses one node line of its kind into n; the shape is checked here, the tree checks come in check_tree.
int read_free_node(const std::vector<std::string_view> &p, rv_editor_tile_node &n, std::string &error)
{
    if (p.size() != layout_pane_field_count) {
        error = "free node line has a wrong field count";
        return RV_ERR_INVAL;
    }
    n.kind = rv_editor_tile_kind::free;
    n.parent = rv_editor_tile_none;
    return RV_OK;
}

int read_leaf_node(const std::vector<std::string_view> &p, rv_editor_tile_node &n, std::string &error)
{
    if (p.size() < layout_leaf_field_min) {
        error = "leaf node line is too short";
        return RV_ERR_INVAL;
    }
    n.kind = rv_editor_tile_kind::leaf;
    if (parse_index(p[2], n.parent) != RV_OK) {
        error = "leaf parent is neither a number nor a dash";
        return RV_ERR_INVAL;
    }
    if (parse_u32(p[3], n.leaf.active) != RV_OK) {
        error = "leaf active tab is not a number";
        return RV_ERR_INVAL;
    }
    for (size_t i = layout_leaf_field_min; i < p.size(); ++i) {
        uint32_t tab_id = 0;
        if (parse_u32(p[i], tab_id) != RV_OK) {
            error = "leaf tab is not a number";
            return RV_ERR_INVAL;
        }
        n.leaf.tabs.push_back(tab_id);
    }
    return RV_OK;
}

int read_split_node(const std::vector<std::string_view> &p, rv_editor_tile_node &n, std::string &error)
{
    if (p.size() != layout_split_field_count) {
        error = "split node line has a wrong field count";
        return RV_ERR_INVAL;
    }
    n.kind = rv_editor_tile_kind::split;
    if (parse_index(p[layout_split_field_parent], n.parent) != RV_OK) {
        error = "split parent is neither a number nor a dash";
        return RV_ERR_INVAL;
    }
    rv_editor_axis a;
    if (axis_from_name(p[layout_split_field_axis], a) != RV_OK) {
        error = "split axis is neither x nor y";
        return RV_ERR_INVAL;
    }
    n.split.axis = a;
    if (parse_f32(p[layout_split_field_ratio], n.split.ratio) != RV_OK) {
        error = "split ratio is not a number";
        return RV_ERR_INVAL;
    }
    if (parse_u32(p[layout_split_field_first], n.split.first) != RV_OK) {
        error = "split first child is not a number";
        return RV_ERR_INVAL;
    }
    if (parse_u32(p[layout_split_field_second], n.split.second) != RV_OK) {
        error = "split second child is not a number";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

using node_reader = int (*)(const std::vector<std::string_view> &p, rv_editor_tile_node &n, std::string &error);

struct node_reader_entry {
    std::string_view type;
    node_reader read;
};

// The node type word (field 1) picks the reader.
constexpr node_reader_entry node_readers[] = {
    { layout_node_type_free, read_free_node },
    { layout_node_type_leaf, read_leaf_node },
    { layout_node_type_split, read_split_node },
};

// The caller has checked that the line has at least layout_node_field_min fields.
int read_node(const std::vector<std::string_view> &p, rv_editor_tile_node &n, std::string &error)
{
    for (const auto &entry : node_readers) {
        if (p[1] == entry.type) {
            return entry.read(p, n, error);
        }
    }
    error = "node type is unknown";
    return RV_ERR_INVAL;
}

// Node lines up to the first line that is not one, in line order.
int read_nodes(const std::vector<std::string_view> &lines, size_t &idx, rv_editor_layout &nl, std::string &error)
{
    while (idx < lines.size()) {
        auto p = split_line(lines[idx]);
        if (p.empty() || p[0] != layout_keyword_node) {
            break;
        }
        if (p.size() < layout_node_field_min) {
            error = "node line is too short";
            return RV_ERR_INVAL;
        }
        rv_editor_tile_node n{};
        if (read_node(p, n, error) != RV_OK) {
            return RV_ERR_INVAL;
        }
        nl.nodes.push_back(n);
        ++idx;
    }
    return RV_OK;
}

// The root line: the index of the root node.
int read_root(const std::vector<std::string_view> &lines, size_t &idx, rv_editor_layout &nl, std::string &error)
{
    if (idx >= lines.size()) {
        error = "root line is missing";
        return RV_ERR_INVAL;
    }
    auto p = split_line(lines[idx]);
    if (p.size() != layout_pane_field_count || p[0] != layout_keyword_root) {
        error = "root line is malformed";
        return RV_ERR_INVAL;
    }
    if (parse_u32(p[1], nl.root) != RV_OK) {
        error = "root index is not a number";
        return RV_ERR_INVAL;
    }
    ++idx;
    return RV_OK;
}

// The maximized line, then the end of the file: no line may follow it.
int read_maximized_and_end(const std::vector<std::string_view> &lines, size_t &idx, rv_editor_layout &nl, std::string &error)
{
    if (idx >= lines.size()) {
        error = "maximized line is missing";
        return RV_ERR_INVAL;
    }
    auto p = split_line(lines[idx]);
    if (p.size() != layout_pane_field_count || p[0] != layout_keyword_maximized) {
        error = "maximized line is malformed";
        return RV_ERR_INVAL;
    }
    if (parse_index(p[1], nl.maximized_leaf) != RV_OK) {
        error = "maximized index is neither a number nor a dash";
        return RV_ERR_INVAL;
    }
    ++idx;
    if (idx != lines.size()) {
        error = "text follows the maximized line";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Tree checks over the whole layout: the root, leaves, splits, parent links, reachability from the root,
// and the maximized leaf. A pane is shown at most once.
int check_tree(const rv_editor_pane_registry &np, const rv_editor_layout &nl, std::string &error)
{
    if (nl.nodes.empty() || nl.root >= nl.nodes.size()) {
        error = "root is outside the node list";
        return RV_ERR_INVAL;
    }
    if (nl.nodes[nl.root].kind == rv_editor_tile_kind::free || nl.nodes[nl.root].parent != rv_editor_tile_none) {
        error = "root is a free slot or has a parent";
        return RV_ERR_INVAL;
    }
    uint32_t pc = static_cast<uint32_t>(np.panes.size());
    std::vector<bool> pu(pc, false);
    for (uint32_t i = 0; i < nl.nodes.size(); ++i) {
        const auto &n = nl.nodes[i];
        if (n.kind == rv_editor_tile_kind::leaf) {
            if ((n.leaf.tabs.empty() && n.leaf.active != 0) || (!n.leaf.tabs.empty() && n.leaf.active >= n.leaf.tabs.size())) {
                error = "leaf active tab is outside its tabs";
                return RV_ERR_INVAL;
            }
            for (auto x : n.leaf.tabs) {
                if (x >= pc) {
                    error = "leaf shows a pane that the registry does not have";
                    return RV_ERR_INVAL;
                }
                if (pu[x]) {
                    error = "pane is shown in two leaves";
                    return RV_ERR_INVAL;
                }
                pu[x] = true;
            }
        } else if (n.kind == rv_editor_tile_kind::split) {
            // The axis was read by name, so it needs no check here.
            if (!std::isfinite(n.split.ratio) || n.split.ratio < layout_ratio_min || n.split.ratio > layout_ratio_max) {
                error = "split ratio is outside the range from 0 to 1";
                return RV_ERR_INVAL;
            }
            if (n.split.first == n.split.second || n.split.first >= nl.nodes.size() || n.split.second >= nl.nodes.size()) {
                error = "split children are the same node or outside the node list";
                return RV_ERR_INVAL;
            }
            if (nl.nodes[n.split.first].kind == rv_editor_tile_kind::free ||
                nl.nodes[n.split.second].kind == rv_editor_tile_kind::free) {
                error = "split child is a free slot";
                return RV_ERR_INVAL;
            }
            if (nl.nodes[n.split.first].parent != i || nl.nodes[n.split.second].parent != i) {
                error = "split child does not name the split as its parent";
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
            error = "node has no parent or its parent is outside the node list";
            return RV_ERR_INVAL;
        }
        const auto &pn = nl.nodes[n.parent];
        if (pn.kind != rv_editor_tile_kind::split || (pn.split.first != i && pn.split.second != i)) {
            error = "parent is not a split that lists the node";
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
            error = "node is not reachable from the root";
            return RV_ERR_INVAL;
        }
    }
    if (nl.maximized_leaf != rv_editor_tile_none &&
        (nl.maximized_leaf >= nl.nodes.size() || nl.nodes[nl.maximized_leaf].kind != rv_editor_tile_kind::leaf)) {
        error = "maximized node is not a leaf of the layout";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

} // namespace

// Publishes the parsed panes and layout only when every step passes. The reason of a refusal is dropped
// here: the public contract keeps its signature, so the caller gets RV_ERR_INVAL alone.
int rv_editor_layout_read(std::string_view text, rv_editor_pane_registry &panes, rv_editor_layout &layout)
{
    const auto lines = split_text(text);
    size_t idx = 0;
    std::string error;
    rv_editor_pane_registry np;
    rv_editor_layout nl;
    if (read_header(lines, idx, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (read_panes(lines, idx, np, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (read_nodes(lines, idx, nl, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (read_root(lines, idx, nl, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (read_maximized_and_end(lines, idx, nl, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (check_tree(np, nl, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    panes = np;
    layout = nl;
    return RV_OK;
}

} // namespace rv_editor
