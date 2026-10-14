// Text form of the workspace, write side: rv_editor_layout_write. The read side is in rv_editor_layout_read.cpp.

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
// Precondition: check_tree has passed, so every leaf tab is a pane of the registry.
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
            const std::string_view kind = kind_name(panes.panes[x].kind);
            if (kind.empty()) {
                error = "pane " + std::to_string(x) + " has an unknown kind";
                return RV_ERR_INVAL;
            }
            renumber[x] = written++;
            lines += std::string(layout_keyword_pane) + std::string(layout_field_sep) + std::string(kind) +
                std::string(layout_line_end);
        }
    }
    return RV_OK;
}

// One node line of any kind. The tree checks have passed: every leaf tab is a pane of the registry, shown once,
// and the split ratio is in range. `renumber` comes from write_panes.
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

// Root line and maximized line. The tree checks have passed, so both indices are in range.
std::string write_tail(const rv_editor_layout &layout)
{
    const std::string sep(layout_field_sep);
    const std::string line_end(layout_line_end);
    return std::string(layout_keyword_root) + sep + std::to_string(layout.root) + line_end +
        std::string(layout_keyword_maximized) + sep + index_text(layout.maximized_leaf) + line_end;
}

} // namespace

// Runs the tree checks first, so a layout the reader would refuse is never written. Builds the whole text
// before publishing it, so `text` changes only on RV_OK.
int rv_editor_layout_write(const rv_editor_pane_registry &panes,
    const rv_editor_layout &layout,
    std::string &text,
    std::string &error)
{
    if (check_tree(panes, layout, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
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
    text = r + write_tail(layout);
    return RV_OK;
}

} // namespace rv_editor
