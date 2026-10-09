// Text form of the workspace, write side: rv_editor_layout_write. The read side is in rv_editor_layout_io.cpp.

#include "layout/rv_editor_layout_format.hpp"
#include "layout/rv_editor_tile.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_editor
{

using namespace layout_format;

namespace
{

// Header line with its newline.
std::string write_header()
{
    return std::string(rv_editor_layout_header) + std::string(layout_line_end);
}

// An index as written: `-` for rv_editor_tile_none, the number otherwise.
std::string index_text(uint32_t index)
{
    return index == rv_editor_tile_none ? std::string(layout_index_marker) : std::to_string(index);
}

// Pane lines for the panes the tree still shows, in tree order. `renumber` maps a pane id to its
// written number. A closed pane stays in the registry for the session, never in the file.
int write_panes(const rv_editor_pane_registry &panes,
    const rv_editor_layout &layout,
    std::vector<uint32_t> &renumber,
    std::string &lines,
    std::string &error)
{
    renumber.assign(panes.panes.size(), rv_editor_tile_none);
    uint32_t written = 0;
    for (const auto &n : layout.nodes) {
        if (n.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id x : n.leaf.tabs) {
            if (x >= panes.panes.size()) {
                error = "layout shows pane " + std::to_string(x) + " that the registry does not have";
                return RV_ERR_INVAL;
            }
            const std::string_view kind = kind_name(panes.panes[x].kind);
            if (kind.empty()) {
                error = "pane " + std::to_string(x) + " has an unknown kind";
                return RV_ERR_INVAL;
            }
            if (renumber[x] != rv_editor_tile_none) {
                error = "pane " + std::to_string(x) + " is shown in two leaves";
                return RV_ERR_INVAL;
            }
            renumber[x] = written++;
            lines += std::string(layout_keyword_pane) + std::string(layout_field_sep) + std::string(kind) +
                std::string(layout_line_end);
        }
    }
    return RV_OK;
}

// One node line of any kind. `renumber` comes from write_panes, which checked every leaf tab.
int write_node(const rv_editor_tile_node &n, const std::vector<uint32_t> &renumber, std::string &line, std::string &error)
{
    const std::string keyword = std::string(layout_keyword_node) + std::string(layout_field_sep);
    const std::string sep(layout_field_sep);
    const std::string line_end(layout_line_end);
    switch (n.kind) {
    case rv_editor_tile_kind::free:
        line = keyword + std::string(layout_node_type_free) + line_end;
        return RV_OK;
    case rv_editor_tile_kind::leaf: {
        std::string r =
            keyword + std::string(layout_node_type_leaf) + sep + index_text(n.parent) + sep + std::to_string(n.leaf.active);
        for (auto x : n.leaf.tabs) {
            r += sep + std::to_string(renumber[x]);
        }
        line = r + line_end;
        return RV_OK;
    }
    case rv_editor_tile_kind::split: {
        // NaN fails both comparisons, so it is refused too.
        if (!(n.split.ratio >= layout_ratio_min && n.split.ratio <= layout_ratio_max)) {
            error = "split ratio is outside the range from 0 to 1";
            return RV_ERR_INVAL;
        }
        char ratio[layout_ratio_format_buf_size];
        const int written = std::snprintf(ratio, sizeof(ratio), "%.4f", n.split.ratio);
        if (written < 0 || written >= static_cast<int>(sizeof(ratio))) {
            error = "split ratio does not format";
            return RV_ERR_INVAL;
        }
        const std::string axis = n.split.axis == rv_editor_axis::x ? std::string(layout_axis_x) : std::string(layout_axis_y);
        line = keyword + std::string(layout_node_type_split) + sep + index_text(n.parent) + sep + axis + sep + ratio + sep +
            std::to_string(n.split.first) + sep + std::to_string(n.split.second) + line_end;
        return RV_OK;
    }
    }
    error = "node kind is unknown";
    return RV_ERR_INVAL;
}

// Root line and maximized line. Refuses what the reader refuses: the root must be a node that is
// not free and has no parent, the maximized node must be a leaf.
int write_tail(const rv_editor_layout &layout, std::string &tail, std::string &error)
{
    if (layout.root >= layout.nodes.size()) {
        error = "root is outside the node list";
        return RV_ERR_INVAL;
    }
    const rv_editor_tile_node &root = layout.nodes[layout.root];
    if (root.kind == rv_editor_tile_kind::free || root.parent != rv_editor_tile_none) {
        error = "root is a free slot or has a parent";
        return RV_ERR_INVAL;
    }
    if (layout.maximized_leaf != rv_editor_tile_none &&
        (layout.maximized_leaf >= layout.nodes.size() ||
            layout.nodes[layout.maximized_leaf].kind != rv_editor_tile_kind::leaf)) {
        error = "maximized node is not a leaf of the layout";
        return RV_ERR_INVAL;
    }
    const std::string sep(layout_field_sep);
    const std::string line_end(layout_line_end);
    tail = std::string(layout_keyword_root) + sep + std::to_string(layout.root) + line_end +
        std::string(layout_keyword_maximized) + sep + index_text(layout.maximized_leaf) + line_end;
    return RV_OK;
}

} // namespace

// Builds the whole text before publishing it, so `text` changes only on RV_OK. Checks the panes,
// the nodes (kind, ratio, formatting), the root and the maximized node. Not checked yet: the leaf
// active index, parent-child links, reachability from the root; the reader's shared checks for
// these come in a later task.
int rv_editor_layout_write(const rv_editor_pane_registry &panes,
    const rv_editor_layout &layout,
    std::string &text,
    std::string &error)
{
    std::vector<uint32_t> renumber;
    std::string pane_text;
    const int panes_rc = write_panes(panes, layout, renumber, pane_text, error);
    if (panes_rc != RV_OK) {
        return panes_rc;
    }
    std::string r = write_header() + pane_text;
    for (const auto &n : layout.nodes) {
        std::string line;
        const int node_rc = write_node(n, renumber, line, error);
        if (node_rc != RV_OK) {
            return node_rc;
        }
        r += line;
    }
    std::string tail;
    const int tail_rc = write_tail(layout, tail, error);
    if (tail_rc != RV_OK) {
        return tail_rc;
    }
    text = r + tail;
    return RV_OK;
}

} // namespace rv_editor
