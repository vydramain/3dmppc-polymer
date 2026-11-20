// The Burn preset's tree: the candidate (with Export) beside the release controls over the game, on top; the logs
// as one strip across the full width at the bottom.

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

    // Root: the work area on top, the logs strip across the full width below it.
    RV_EDITOR_CHECK(is_split(l, l.root, rv_editor_axis::y));
    if (!is_split(l, l.root, rv_editor_axis::y)) {
        return;
    }
    const rv_editor_tile_split root = l.nodes[l.root].split;

    // Logs: the build log (+ runtime log) alone as the bottom leaf.
    RV_EDITOR_CHECK(is_leaf_of(l, p, root.second, { rv_editor_pane_kind::build_log, rv_editor_pane_kind::runtime_log }));

    // Work area: candidate (+ Export) beside a column of release controls over the game.
    RV_EDITOR_CHECK(is_split(l, root.first, rv_editor_axis::x));
    if (!is_split(l, root.first, rv_editor_axis::x)) {
        return;
    }
    const rv_editor_tile_split work = l.nodes[root.first].split;
    RV_EDITOR_CHECK(is_leaf_of(l, p, work.first, { rv_editor_pane_kind::candidate, rv_editor_pane_kind::export_ }));

    // Right column: the release controls strip over the game alone.
    RV_EDITOR_CHECK(is_split(l, work.second, rv_editor_axis::y));
    if (!is_split(l, work.second, rv_editor_axis::y)) {
        return;
    }
    const rv_editor_tile_split right = l.nodes[work.second].split;
    RV_EDITOR_CHECK(is_leaf_of(l, p, right.first, { rv_editor_pane_kind::release_controls }));
    RV_EDITOR_CHECK(is_leaf_of(l, p, right.second, { rv_editor_pane_kind::game }));
}
