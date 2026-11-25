// The Game tile's fit: the picture area takes the project's screen aspect, as large as the other tiles' minimums let it.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>

#include "app/rv_editor_shell.hpp"

namespace rv_editor
{

namespace
{

// Sets the ratios so that `node` is `want` px along `axis`, walking up from it. A split whose other child is a strip
// keeps the strip's pixels (in `at`, the tree as placed) and hands the change on; the first free split takes the rest.
// False when no split can: the want does not fit.
bool rv_editor_fit_axis(rv_editor_layout &layout,
    const std::vector<rv_editor_rect> &at,
    int32_t bar,
    uint32_t node,
    rv_editor_axis axis,
    int32_t want,
    const std::function<bool(uint32_t)> &strip_like)
{
    for (uint32_t parent = layout.nodes[node].parent; parent != rv_editor_tile_none;
        node = parent, parent = layout.nodes[parent].parent) {
        const rv_editor_tile_split &split = layout.nodes[parent].split;
        if (split.axis != axis) {
            continue;
        }
        const bool first = node == split.first;
        const uint32_t sibling = first ? split.second : split.first;
        const bool along_x = axis == rv_editor_axis::x;
        const int32_t strip = strip_like(sibling) ? (along_x ? at[sibling].w : at[sibling].h) : -1;
        const int32_t avail = strip >= 0 ? want + strip : (along_x ? at[parent].w : at[parent].h) - bar;
        if (want < 0 || want > avail || avail <= 0) {
            return false;
        }
        const int32_t mine = first ? want : avail - want;
        (void)rv_editor_tile_set_ratio(layout, parent, static_cast<float>(mine) / static_cast<float>(avail));
        if (strip < 0) {
            return true;
        }
        want = avail + bar;
    }
    return false;
}

// Metrics and minimums for a tree no draw has measured (the view stores them in the workspace at every draw, so the
// product never gets here; the unit tests place trees by hand and set only `minimums`). They are derived from the
// rectangles: the bar from the root's gap, the chrome from the Game's tile less its area and the shelf its minimum
// holds above the picture, the tab strip from the leaf with the least room beyond its pane and chrome.
void rv_editor_infer_metrics(const rv_editor_workspace &ws,
    rv_editor_pane_id game,
    rv_editor_size over,
    int32_t screen_h,
    rv_editor_tile_metrics &metrics,
    std::vector<rv_editor_size> &pane_min)
{
    pane_min.assign(ws.panes.panes.size(), rv_editor_size{ 0, 0 });
    for (const auto &[pane, min] : ws.minimums) {
        pane_min[pane] = min;
    }
    const rv_editor_tile_split &root = ws.layout.nodes[ws.layout.root].split;
    const rv_editor_rect &ra = ws.rects[root.first];
    const rv_editor_rect &rb = ws.rects[root.second];
    const int32_t bar = root.axis == rv_editor_axis::x ? rb.x - ra.x - ra.w : rb.y - ra.y - ra.h;
    const int32_t shelf = std::max<int32_t>(0, pane_min[game].h - screen_h);
    metrics = { bar, { over.w, std::max(0, over.h - shelf) }, 0 };
    // No leaf is below its minimum, so the least a leaf has beyond its pane and chrome bounds the tab strip, and a leaf
    // at its minimum (a strip, the Game at the start) makes it exact.
    int32_t tabs = std::numeric_limits<int32_t>::max();
    for (uint32_t i = 0; i < ws.layout.nodes.size(); ++i) {
        const rv_editor_tile_node &n = ws.layout.nodes[i];
        if (n.kind == rv_editor_tile_kind::leaf && !n.leaf.tabs.empty()) {
            int32_t inner = 0;
            for (const rv_editor_pane_id pane : n.leaf.tabs) {
                inner = std::max(inner, pane_min[pane].h);
            }
            tabs = std::min(tabs, ws.rects[i].h - inner - metrics.chrome.h);
        }
    }
    metrics.tabs = std::max(0, tabs);
}

} // namespace

void rv_editor_fit_game_tree(rv_editor_workspace &ws,
    rv_editor_pane_id game,
    rv_editor_size area,
    int32_t screen_w,
    int32_t screen_h,
    int32_t target_h,
    const std::function<bool(uint32_t)> &strip_like)
{
    const uint32_t node = rv_editor_tile_find(ws.layout, game);
    if (node == rv_editor_tile_none || ws.layout.nodes[ws.layout.root].kind != rv_editor_tile_kind::split) {
        return;
    }
    const rv_editor_rect game_rect = ws.rects[node];
    const rv_editor_size over{ game_rect.w - area.w, game_rect.h - area.h };
    std::vector<rv_editor_size> pane_min = ws.pane_min;
    rv_editor_tile_metrics metrics = ws.tile_metrics;
    if (pane_min.size() != ws.panes.panes.size()) {
        rv_editor_infer_metrics(ws, game, over, screen_h, metrics, pane_min);
    }
    const auto place = [&](const rv_editor_layout &layout) {
        std::vector<rv_editor_rect> at(layout.nodes.size());
        for (const auto &p : rv_editor_layout_place(layout, ws.rects[layout.root], metrics, pane_min)) {
            at[p.node] = p.rect;
        }
        return at;
    };
    const std::vector<rv_editor_rect> now = place(ws.layout);
    // Aspect steps gcd-reduced from the screen; the area is k of them, k the largest the other tiles' minimums let by,
    // but no more than `target_h` asks for (a window that is bigger gives the room to the other tiles).
    const int64_t sw = std::max<int64_t>(1, screen_w);
    const int64_t sh = std::max<int64_t>(1, screen_h);
    const int32_t step_w = static_cast<int32_t>(sw / std::gcd(sw, sh));
    const int32_t step_h = static_cast<int32_t>(sh / std::gcd(sw, sh));
    // Candidate trees for k steps: the Game's tile set to the area's size plus its overhead in one go, then placed to see
    // that the minimums let it keep it. Returns 0 when the area is exact, -1 when a minimum (a strip's width) makes it
    // larger than k steps, +1 when it cannot be that large: the exact k are an interval, so a bisection finds its top.
    const auto candidate = [&](int32_t k, rv_editor_layout &tree) {
        tree = ws.layout;
        const bool set = rv_editor_fit_axis(tree, now, metrics.bar, node, rv_editor_axis::x, k * step_w + over.w, strip_like) &&
            rv_editor_fit_axis(tree, now, metrics.bar, node, rv_editor_axis::y, k * step_h + over.h, strip_like);
        const rv_editor_rect g = set ? place(tree)[node] : rv_editor_rect{ 0, 0, 0, 0 };
        if (!set) {
            return 1;
        }
        const int32_t dw = g.w - over.w - k * step_w;
        const int32_t dh = g.h - over.h - k * step_h;
        return dw == 0 && dh == 0 ? 0 : (dw > 0 || dh > 0 ? -1 : 1);
    };
    // The Game's own minimum bounds k from below, so the bisection needs its low end.
    const rv_editor_size least = rv_editor_tile_min_size(ws.layout, node, metrics, pane_min);
    int32_t low = std::max({ 1, (least.w - over.w + step_w - 1) / step_w, (least.h - over.h + step_h - 1) / step_h });
    int32_t high = std::min(ws.rects[ws.layout.root].w / step_w, ws.rects[ws.layout.root].h / step_h);
    high = std::min(high, std::max(low, target_h / step_h));
    rv_editor_layout best;
    rv_editor_layout tree;
    while (low < high) {
        const int32_t k = (low + high + 1) / 2;
        if (candidate(k, tree) <= 0) {
            low = k;
        } else {
            high = k - 1;
        }
    }
    if (low > high || candidate(low, best) != 0) {
        // No exact area fits (a window at the tree's minimum): the width follows the height the tree gives, so no bands.
        const int32_t height = now[node].h - over.h;
        best = ws.layout;
        if (height > 0 &&
            rv_editor_fit_axis(best,
                now,
                metrics.bar,
                node,
                rv_editor_axis::x,
                (height * step_w + step_h - 1) / step_h + over.w,
                strip_like)) {
            ws.layout = std::move(best);
        }
        return;
    }
    ws.layout = std::move(best);
}

} // namespace rv_editor
