// The Burn preset's tree: release controls as a full-width strip on top; under it the candidate (with Export) over
// the logs on the left, and the game spanning the whole remaining height on the right.

#include "layout/rv_editor_tile.hpp"
#include "rv_editor_test.hpp"

using namespace rv_editor;

namespace
{

bool is_split(const rv_editor_layout &l, uint32_t n, rv_editor_axis axis)
{
    return n < l.nodes.size() && l.nodes[n].kind == rv_editor_tile_kind::split && l.nodes[n].split.axis == axis;
}

// True when `n` is a leaf whose tabs are exactly `kinds`, in order.
bool is_leaf_of(const rv_editor_layout &l, const rv_editor_pane_registry &p, uint32_t n, std::vector<rv_editor_pane_kind> kinds)
{
    if (n >= l.nodes.size() || l.nodes[n].kind != rv_editor_tile_kind::leaf || l.nodes[n].leaf.tabs.size() != kinds.size()) {
        return false;
    }
    for (size_t i = 0; i < kinds.size(); ++i) {
        if (p.panes[l.nodes[n].leaf.tabs[i]].kind != kinds[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_layout_burn_tree)
{
    rv_editor_pane_registry p;
    rv_editor_layout l;
    rv_editor_layout_preset_make(rv_editor_layout_preset::burn, p, l);

    // Root: vertical stack, the release controls strip on top.
    RV_EDITOR_CHECK(is_split(l, l.root, rv_editor_axis::y));
    if (!is_split(l, l.root, rv_editor_axis::y)) {
        return;
    }
    const rv_editor_tile_split root = l.nodes[l.root].split;
    RV_EDITOR_CHECK(is_leaf_of(l, p, root.first, { rv_editor_pane_kind::release_controls }));

    // Below: side by side.
    RV_EDITOR_CHECK(is_split(l, root.second, rv_editor_axis::x));
    if (!is_split(l, root.second, rv_editor_axis::x)) {
        return;
    }
    const rv_editor_tile_split body = l.nodes[root.second].split;

    // Right: the game alone, the whole remaining height.
    RV_EDITOR_CHECK(is_leaf_of(l, p, body.second, { rv_editor_pane_kind::game }));

    // Left: candidate (+ Export) over the build log (+ runtime log).
    RV_EDITOR_CHECK(is_split(l, body.first, rv_editor_axis::y));
    if (!is_split(l, body.first, rv_editor_axis::y)) {
        return;
    }
    const rv_editor_tile_split column = l.nodes[body.first].split;
    RV_EDITOR_CHECK(is_leaf_of(l, p, column.first, { rv_editor_pane_kind::candidate, rv_editor_pane_kind::export_ }));
    RV_EDITOR_CHECK(is_leaf_of(l, p, column.second, { rv_editor_pane_kind::build_log, rv_editor_pane_kind::runtime_log }));
}
