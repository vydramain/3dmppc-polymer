#include "layout/rv_editor_tile.hpp"

#include <algorithm>
#include <cmath>

namespace rv_editor
{

namespace
{

bool rv_editor_tile_is(const rv_editor_layout &layout, uint32_t node, rv_editor_tile_kind kind)
{
    return node < layout.nodes.size() && layout.nodes[node].kind == kind;
}

// A free slot, reused before the pool grows. Callers take indices, not
// references, across this call: growing the pool moves the nodes.
uint32_t rv_editor_tile_alloc(rv_editor_layout &layout)
{
    for (uint32_t i = 0; i < layout.nodes.size(); ++i) {
        if (layout.nodes[i].kind == rv_editor_tile_kind::free) {
            return i;
        }
    }
    layout.nodes.push_back({});
    return static_cast<uint32_t>(layout.nodes.size() - 1);
}

void rv_editor_tile_free(rv_editor_layout &layout, uint32_t node)
{
    layout.nodes[node] = rv_editor_tile_node{ rv_editor_tile_kind::free, rv_editor_tile_none, {}, {} };
}

// Point whatever referred to `from` (its parent's child slot, or the root) at `to`.
void rv_editor_tile_replace(rv_editor_layout &layout, uint32_t from, uint32_t to)
{
    const uint32_t parent = layout.nodes[from].parent;
    layout.nodes[to].parent = parent;
    if (parent == rv_editor_tile_none) {
        layout.root = to;
        return;
    }
    rv_editor_tile_split &split = layout.nodes[parent].split;
    if (split.first == from) {
        split.first = to;
    } else {
        split.second = to;
    }
}

int32_t rv_editor_size_along(rv_editor_size size, rv_editor_axis axis)
{
    return axis == rv_editor_axis::x ? size.w : size.h;
}

void rv_editor_tile_place_node(const rv_editor_layout &layout, uint32_t node, rv_editor_rect rect,
    const rv_editor_tile_metrics &metrics, const std::vector<rv_editor_size> &pane_min,
    std::vector<rv_editor_tile_place> &out)
{
    const rv_editor_tile_node &n = layout.nodes[node];
    if (n.kind != rv_editor_tile_kind::split) {
        out.push_back({ node, rect, false });
        return;
    }

    const rv_editor_tile_split &split = n.split;
    const bool along_x = split.axis == rv_editor_axis::x;
    const int32_t length = along_x ? rect.w : rect.h;
    const int64_t avail = std::max<int64_t>(0, length - metrics.bar);
    const int64_t min_a =
        rv_editor_size_along(rv_editor_tile_min_size(layout, split.first, metrics, pane_min), split.axis);
    const int64_t min_b =
        rv_editor_size_along(rv_editor_tile_min_size(layout, split.second, metrics, pane_min), split.axis);

    const bool overflow = min_a + min_b > avail;
    int64_t first = 0;
    if (!overflow) {
        const int64_t want = static_cast<int64_t>(std::lround(static_cast<double>(split.ratio) * static_cast<double>(avail)));
        first = std::clamp(want, min_a, avail - min_b);
    } else if (min_a + min_b > 0) {
        first = avail * min_a / (min_a + min_b);
    } else {
        first = avail / 2;
    }
    const int64_t second = avail - first;
    out.push_back({ node, rect, overflow });

    rv_editor_rect a = rect;
    rv_editor_rect b = rect;
    if (along_x) {
        a.w = static_cast<int32_t>(first);
        b.x = rect.x + static_cast<int32_t>(first) + metrics.bar;
        b.w = static_cast<int32_t>(second);
    } else {
        a.h = static_cast<int32_t>(first);
        b.y = rect.y + static_cast<int32_t>(first) + metrics.bar;
        b.h = static_cast<int32_t>(second);
    }
    rv_editor_tile_place_node(layout, split.first, a, metrics, pane_min, out);
    rv_editor_tile_place_node(layout, split.second, b, metrics, pane_min, out);
}

} // namespace

// --- panes --------------------------------------------------------------------

rv_editor_pane_id rv_editor_pane_add(rv_editor_pane_registry &registry, rv_editor_pane_kind kind)
{
    registry.panes.push_back({ kind });
    return static_cast<rv_editor_pane_id>(registry.panes.size() - 1);
}

bool rv_editor_pane_set_kind(rv_editor_pane_registry &registry, rv_editor_pane_id id, rv_editor_pane_kind kind)
{
    if (id >= registry.panes.size()) {
        return false;
    }
    registry.panes[id].kind = kind;
    return true;
}

// --- the tree -----------------------------------------------------------------

rv_editor_layout rv_editor_layout_make(rv_editor_pane_id pane)
{
    rv_editor_layout layout{};
    rv_editor_tile_node leaf{ rv_editor_tile_kind::leaf, rv_editor_tile_none, {}, {} };
    if (pane != rv_editor_tile_none) {
        leaf.leaf.tabs.push_back(pane);
    }
    layout.nodes.push_back(leaf);
    layout.root = 0;
    layout.maximized_leaf = rv_editor_tile_none;
    return layout;
}

uint32_t rv_editor_tile_find(const rv_editor_layout &layout, rv_editor_pane_id pane)
{
    for (uint32_t i = 0; i < layout.nodes.size(); ++i) {
        const rv_editor_tile_node &node = layout.nodes[i];
        if (node.kind == rv_editor_tile_kind::leaf &&
            std::find(node.leaf.tabs.begin(), node.leaf.tabs.end(), pane) != node.leaf.tabs.end()) {
            return i;
        }
    }
    return rv_editor_tile_none;
}

uint32_t rv_editor_tile_insert(rv_editor_layout &layout, uint32_t leaf, rv_editor_pane_id pane,
    rv_editor_tile_dock dock)
{
    if (!rv_editor_tile_is(layout, leaf, rv_editor_tile_kind::leaf)) {
        return rv_editor_tile_none;
    }

    // An empty leaf has nothing to share its place with: the pane simply fills it.
    if (dock == rv_editor_tile_dock::tab || layout.nodes[leaf].leaf.tabs.empty()) {
        rv_editor_tile_leaf &target = layout.nodes[leaf].leaf;
        target.tabs.push_back(pane);
        target.active = static_cast<uint32_t>(target.tabs.size() - 1);
        return leaf;
    }

    const uint32_t added = rv_editor_tile_alloc(layout);
    layout.nodes[added] = rv_editor_tile_node{ rv_editor_tile_kind::leaf, rv_editor_tile_none, {}, { { pane }, 0 } };
    const uint32_t split = rv_editor_tile_alloc(layout);
    layout.nodes[split].kind = rv_editor_tile_kind::split;
    rv_editor_tile_replace(layout, leaf, split);

    const bool across = dock == rv_editor_tile_dock::left || dock == rv_editor_tile_dock::right;
    const bool added_first = dock == rv_editor_tile_dock::left || dock == rv_editor_tile_dock::top;
    layout.nodes[split].split = rv_editor_tile_split{
        across ? rv_editor_axis::x : rv_editor_axis::y,
        0.5f,
        added_first ? added : leaf,
        added_first ? leaf : added,
    };
    layout.nodes[leaf].parent = split;
    layout.nodes[added].parent = split;
    return added;
}

bool rv_editor_tile_remove(rv_editor_layout &layout, rv_editor_pane_id pane)
{
    const uint32_t leaf = rv_editor_tile_find(layout, pane);
    if (leaf == rv_editor_tile_none) {
        return false;
    }

    rv_editor_tile_leaf &tabs = layout.nodes[leaf].leaf;
    const auto at = std::find(tabs.tabs.begin(), tabs.tabs.end(), pane);
    const uint32_t index = static_cast<uint32_t>(at - tabs.tabs.begin());
    tabs.tabs.erase(at);
    if (index < tabs.active || tabs.active >= tabs.tabs.size()) {
        tabs.active = tabs.active > 0 ? tabs.active - 1 : 0;
    }
    if (!tabs.tabs.empty() || leaf == layout.root) {
        return true;
    }

    const uint32_t split = layout.nodes[leaf].parent;
    const rv_editor_tile_split &s = layout.nodes[split].split;
    const uint32_t sibling = s.first == leaf ? s.second : s.first;
    rv_editor_tile_replace(layout, split, sibling);
    rv_editor_tile_free(layout, leaf);
    rv_editor_tile_free(layout, split);
    if (layout.maximized_leaf == leaf) {
        layout.maximized_leaf = rv_editor_tile_none;
    }
    return true;
}

bool rv_editor_tile_move(rv_editor_layout &layout, rv_editor_pane_id pane, uint32_t leaf, rv_editor_tile_dock dock)
{
    const uint32_t from = rv_editor_tile_find(layout, pane);
    if (from == rv_editor_tile_none || !rv_editor_tile_is(layout, leaf, rv_editor_tile_kind::leaf)) {
        return false;
    }
    // Into its own tab strip changes nothing; beside itself needs a second tab to stay behind.
    if (from == leaf && (dock == rv_editor_tile_dock::tab || layout.nodes[from].leaf.tabs.size() < 2)) {
        return false;
    }

    // `leaf` survives the removal: only `from` and its parent split can go, and
    // neither is `leaf` here.
    rv_editor_tile_remove(layout, pane);
    rv_editor_tile_insert(layout, leaf, pane, dock);
    return true;
}

bool rv_editor_tile_activate(rv_editor_layout &layout, rv_editor_pane_id pane)
{
    const uint32_t leaf = rv_editor_tile_find(layout, pane);
    if (leaf == rv_editor_tile_none) {
        return false;
    }
    rv_editor_tile_leaf &tabs = layout.nodes[leaf].leaf;
    tabs.active = static_cast<uint32_t>(std::find(tabs.tabs.begin(), tabs.tabs.end(), pane) - tabs.tabs.begin());
    return true;
}

bool rv_editor_tile_set_ratio(rv_editor_layout &layout, uint32_t split, float ratio)
{
    if (!rv_editor_tile_is(layout, split, rv_editor_tile_kind::split)) {
        return false;
    }
    // NaN fails both comparisons and lands on 0.
    layout.nodes[split].split.ratio = ratio >= 0.0f ? std::min(ratio, 1.0f) : 0.0f;
    return true;
}

bool rv_editor_tile_toggle_maximize(rv_editor_layout &layout, uint32_t leaf)
{
    if (!rv_editor_tile_is(layout, leaf, rv_editor_tile_kind::leaf)) {
        return false;
    }
    layout.maximized_leaf = layout.maximized_leaf == leaf ? rv_editor_tile_none : leaf;
    return true;
}

// --- placement ----------------------------------------------------------------

rv_editor_size rv_editor_tile_min_size(const rv_editor_layout &layout, uint32_t node, const rv_editor_tile_metrics &metrics,
    const std::vector<rv_editor_size> &pane_min)
{
    const rv_editor_tile_node &n = layout.nodes[node];
    if (n.kind == rv_editor_tile_kind::leaf) {
        // Every tab, not just the active one: switching tabs must not reflow the tree.
        rv_editor_size inner{ 0, 0 };
        for (const rv_editor_pane_id pane : n.leaf.tabs) {
            if (pane < pane_min.size()) {
                inner.w = std::max(inner.w, pane_min[pane].w);
                inner.h = std::max(inner.h, pane_min[pane].h);
            }
        }
        return { inner.w + metrics.chrome.w, inner.h + metrics.chrome.h };
    }

    const rv_editor_size a = rv_editor_tile_min_size(layout, n.split.first, metrics, pane_min);
    const rv_editor_size b = rv_editor_tile_min_size(layout, n.split.second, metrics, pane_min);
    if (n.split.axis == rv_editor_axis::x) {
        return { a.w + metrics.bar + b.w, std::max(a.h, b.h) };
    }
    return { std::max(a.w, b.w), a.h + metrics.bar + b.h };
}

std::vector<rv_editor_tile_place> rv_editor_layout_place(const rv_editor_layout &layout, rv_editor_rect area,
    const rv_editor_tile_metrics &metrics, const std::vector<rv_editor_size> &pane_min)
{
    std::vector<rv_editor_tile_place> out;
    if (rv_editor_tile_is(layout, layout.maximized_leaf, rv_editor_tile_kind::leaf)) {
        out.push_back({ layout.maximized_leaf, area, false });
        return out;
    }
    rv_editor_tile_place_node(layout, layout.root, area, metrics, pane_min, out);
    return out;
}

} // namespace rv_editor
