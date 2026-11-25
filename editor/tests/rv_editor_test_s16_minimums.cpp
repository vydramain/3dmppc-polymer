// S16, R1/R3/R6, AC1: Scene, Hierarchy and Inspector each have one minimum that no state changes and no drag or window
// size goes under, and the body of their tile has no scrolling at that minimum.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s16.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "ui/rv_editor_widgets_content.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s16;

namespace
{

// The minimum the shell would set for pane 1 of `shell` now, measured inside a frame as the shell does.
stable_tiles::minimum minimum_now(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind)
{
    stable_tiles::minimum result{ 0, 0 };
    gui.frame([&] {
        shell.ws.minimums.clear();
        rv_editor_shell_set_minimum(shell, pane, kind, { 0, 0 });
        const auto it = shell.ws.minimums.find(pane);
        if (it != shell.ws.minimums.end()) {
            result = { static_cast<float>(it->second.w), static_cast<float>(it->second.h) };
        }
    });
    return result;
}

// What a pane callback saw of its body window.
struct seen {
    rv_editor_shell *shell = nullptr;
    std::map<rv_editor_pane_id, ImVec2> size;
    std::map<rv_editor_pane_id, ImVec2> overflow; // drawn content less the room, per axis
};

void record_pane(void *context, rv_editor_pane_id id, rv_editor_pane_kind kind, const rv_editor_theme &theme)
{
    auto *s = static_cast<seen *>(context);
    rv_editor_shell_pane(s->shell, id, kind, theme);
    const ImGuiWindow *w = ImGui::GetCurrentWindow();
    s->size[id] = w->Size;
    s->overflow[id] =
        ImVec2(w->DC.CursorMaxPos.x - w->DC.CursorStartPos.x + w->WindowPadding.x * 2.0f - w->InnerRect.GetWidth(),
            w->DC.CursorMaxPos.y - w->DC.CursorStartPos.y + w->WindowPadding.y * 2.0f - w->InnerRect.GetHeight());
}

void record_size_only(void *context, rv_editor_pane_id id, rv_editor_pane_kind, const rv_editor_theme &)
{
    static_cast<seen *>(context)->size[id] = ImGui::GetCurrentWindow()->Size;
}

} // namespace

// AC2: one minimum per kind, the same in every state, and tall enough for a content area.
RV_EDITOR_TEST(rv_editor_test_s16_every_kind_has_one_minimum_whatever_the_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        fx f(gui, kind);
        const auto first = minimum_now(gui, f.shell, kind);
        if (first.w <= 0.0f || first.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own (%.0f x %.0f)\n", name_of(kind), first.w, first.h);
        }
        RV_EDITOR_CHECK(first.w > 0.0f && first.h > 0.0f);
        float content_min = 0.0f;
        gui.frame([&] {
            content_min = rv_editor_content_min_height();
        });
        // R19: no area is a box of three rows any more; Hierarchy's tree area keeps two (S13.2, Scene's column is 672 px).
        if (kind == rv_editor_pane_kind::hierarchy) {
            gui.frame([&] {
                content_min -= ImGui::GetTextLineHeightWithSpacing();
            });
        }
        RV_EDITOR_CHECK(first.h >= content_min);
        for (size_t i = 0; i < f.states.size(); ++i) {
            f.states[i]();
            f.draw();
            f.draw();
            const auto now = minimum_now(gui, f.shell, kind);
            if (now.w != first.w || now.h != first.h) {
                std::fprintf(stderr,
                    "  %s state %zu: minimum %.0f x %.0f became %.0f x %.0f\n",
                    name_of(kind),
                    i,
                    first.w,
                    first.h,
                    now.w,
                    now.h);
            }
            RV_EDITOR_CHECK(now.w == first.w && now.h == first.h);
        }
    }
}

// AC2: a splitter dragged to either end, or a window smaller than the tree, leaves each tile at least its minimum.
RV_EDITOR_TEST(rv_editor_test_s16_a_drag_or_a_small_window_keeps_each_tile_at_its_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        for (const rv_editor_tile_dock dock : { rv_editor_tile_dock::right, rv_editor_tile_dock::bottom }) {
            rv_editor_shell shell;
            rv_editor_pane_add(shell.ws.panes, rv_editor_pane_kind::empty); // pane 0: the panes below are 1 and 2
            const rv_editor_pane_id a = rv_editor_pane_add(shell.ws.panes, kind);
            const rv_editor_pane_id b = rv_editor_pane_add(shell.ws.panes, kind);
            shell.ws.layout = rv_editor_layout_make(a);
            rv_editor_tile_insert(shell.ws.layout, shell.ws.layout.root, b, dock);
            shell.ws.focused_leaf = shell.ws.layout.root;
            for (const float ratio : { 0.02f, 0.98f }) {
                for (const ImVec2 window : { ImVec2(300, 200), ImVec2(1600, 900) }) {
                    for (uint32_t n = 0; n < shell.ws.layout.nodes.size(); ++n) {
                        if (shell.ws.layout.nodes[n].kind == rv_editor_tile_kind::split) {
                            RV_EDITOR_CHECK(rv_editor_tile_set_ratio(shell.ws.layout, n, ratio) == RV_OK);
                        }
                    }
                    seen s;
                    s.shell = &shell;
                    for (int frame = 0; frame < 3; ++frame) {
                        gui.frame(
                            [&] {
                                shell.ws.minimums.clear();
                                rv_editor_shell_set_minimum(shell, a, kind, { 0, 0 });
                                rv_editor_shell_set_minimum(shell, b, kind, { 0, 0 });
                                std::string error;
                                rv_editor_workspace_draw(shell.ws,
                                    stable_tiles::theme,
                                    record_size_only,
                                    nullptr,
                                    &s,
                                    { 0, 0, static_cast<int>(window.x), static_cast<int>(window.y) },
                                    error);
                            },
                            window.x,
                            window.y);
                    }
                    for (const rv_editor_pane_id id : { a, b }) {
                        const ImVec2 size = s.size[id];
                        if (size.x < min.w || size.y < min.h) {
                            std::fprintf(stderr,
                                "  %s ratio %.2f window %.0fx%.0f: tile body %.0fx%.0f under the minimum %.0fx%.0f\n",
                                name_of(kind),
                                ratio,
                                window.x,
                                window.y,
                                size.x,
                                size.y,
                                min.w,
                                min.h);
                        }
                        RV_EDITOR_CHECK(size.x >= min.w && size.y >= min.h);
                    }
                }
            }
        }
    }
}

// R3, AC1: the body of the tile (the "##pane" window) of a converted kind is drawn at the tile's minimum in every state
// and has no content past its edges, so it shows no scrollbar.
RV_EDITOR_TEST(rv_editor_test_s16_the_tile_body_never_scrolls_at_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        fx f(gui, kind);
        rv_editor_pane_add(f.shell.ws.panes, rv_editor_pane_kind::empty); // pane 0: the tile's pane is 1
        RV_EDITOR_CHECK(rv_editor_pane_add(f.shell.ws.panes, kind) == pane);
        f.shell.ws.layout = rv_editor_layout_make(pane);
        f.shell.ws.focused_leaf = f.shell.ws.layout.root;
        seen s;
        s.shell = &f.shell;
        const auto tile = [&](float width, float height) {
            gui.frame(
                [&] {
                    f.shell.ws.minimums.clear();
                    rv_editor_shell_set_minimum(f.shell, pane, kind, { 0, 0 });
                    std::string error;
                    rv_editor_workspace_draw(f.shell.ws,
                        stable_tiles::theme,
                        record_pane,
                        nullptr,
                        &s,
                        { 0, 0, static_cast<int>(width), static_cast<int>(height) },
                        error);
                },
                width,
                height);
        };
        // A window smaller than any minimum: the tile takes its minimum, and the body is what is left of it.
        for (int i = 0; i < 3; ++i) {
            tile(20, 20);
        }
        const ImVec2 body = s.size[pane];
        const auto min = minimum_now(gui, f.shell, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        RV_EDITOR_CHECK(body.x >= min.w && body.y >= min.h);
        for (size_t i = 0; i < f.states.size(); ++i) {
            f.states[i]();
            for (int frame = 0; frame < 3; ++frame) {
                tile(20, 20);
            }
            const ImVec2 over = s.overflow[pane];
            if (over.x > 0.5f || over.y > 0.5f) {
                std::fprintf(stderr,
                    "  %s state %zu: content past the body by %.1f x %.1f px (body %.0fx%.0f)\n",
                    name_of(kind),
                    i,
                    over.x,
                    over.y,
                    s.size[pane].x,
                    s.size[pane].y);
            }
            RV_EDITOR_CHECK(over.x <= 0.5f && over.y <= 0.5f);
        }
    }
}

// S13.2.1: Hierarchy's minimum is the window's padding, the shelf (one row: the buttons, the search and the order), a one-line
// strip with its gap and the tree area at two rows (the floor less one row), so Scene's right column fits 672 px. The shelf's
// own trailing spacing (the gap to the tree area) may be left out of the minimum when the tree area sits on the shelf.
RV_EDITOR_TEST(rv_editor_test_s16_the_hierarchy_minimum_is_two_rows_and_a_one_line_strip)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::hierarchy);
    float want = 0.0f, gap = 0.0f;
    gui.frame([&] {
        const ImGuiStyle &style = ImGui::GetStyle();
        gap = style.ItemSpacing.y;
        want = style.WindowPadding.y + rv_editor_tiled_shelf_block() + rv_editor_tiled_strip_block(false, 1) +
            rv_editor_tiled_floor() - ImGui::GetTextLineHeightWithSpacing();
    });
    if (min.h > want + 1.5f || min.h < want - gap - 1.5f) {
        std::fprintf(stderr,
            "  hierarchy: the minimum is %.0f px high, the parts make %.0f (%.0f without the shelf's spacing)\n",
            min.h,
            want,
            want - gap);
    }
    RV_EDITOR_CHECK(min.h <= want + 1.5f);
    RV_EDITOR_CHECK(min.h >= want - gap - 1.5f);
}

// S13.2.1: at its least width (Scene's column tile of 336 less 14 of chrome is 322) the Hierarchy shelf is one row: the seven
// buttons, the search field (at least five characters) and the order choice (its longest item, whole) stand at one height
// and show whole.
RV_EDITOR_TEST(rv_editor_test_s16_the_hierarchy_shelf_is_one_row_at_the_minimum_width)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::hierarchy);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    if (min.w > 322.0f) {
        std::fprintf(stderr, "  hierarchy: the minimum width is %.0f, Scene's column leaves 322\n", min.w);
    }
    RV_EDITOR_CHECK(min.w <= 322.0f);
    rv_editor_shell shell;
    rv_editor_pane_add(shell.ws.panes, rv_editor_pane_kind::empty); // pane 0: the pane below is 1
    RV_EDITOR_CHECK(rv_editor_pane_add(shell.ws.panes, rv_editor_pane_kind::hierarchy) == pane);
    rv_editor_probe_record record;
    float query_least = 0.0f, sort_least = 0.0f;
    for (int i = 0; i < 3; ++i) {
        record = rv_editor_probe_draw(
            gui,
            min.w,
            [&](rv_editor_probe_record &) {
                const ImGuiStyle &style = ImGui::GetStyle();
                query_least = 5.0f * ImGui::CalcTextSize("M").x + style.FramePadding.x * 2.0f;
                sort_least = ImGui::CalcTextSize(rv_editor_text("pane_hierarchy.sort_file_order")).x + ImGui::GetFrameHeight() +
                    style.FramePadding.x * 2.0f;
                rv_editor_shell_pane(&shell, pane, rv_editor_pane_kind::hierarchy, stable_tiles::theme);
            },
            min.h);
    }
    rv_editor_probe_record row; // the shelf's items and the form's
    int buttons = 0;
    const rv_editor_probe_entry *query = nullptr, *order = nullptr;
    for (const auto &e : record.entries) {
        if (e.label.rfind("##add", 0) == 0 || e.label.rfind("##command", 0) == 0) {
            ++buttons;
            row.entries.push_back(e);
        } else if (e.label.rfind("##hierarchy_query", 0) == 0) {
            query = &e;
            row.entries.push_back(e);
        } else if (e.label.rfind("##hierarchy_sort", 0) == 0) {
            order = &e;
            row.entries.push_back(e);
        }
    }
    RV_EDITOR_CHECK(buttons == 7);
    RV_EDITOR_CHECK(query != nullptr && order != nullptr);
    if (query == nullptr || order == nullptr) {
        return;
    }
    float y_min = query->y0, y_max = query->y0;
    for (const auto &e : row.entries) {
        y_min = std::min(y_min, e.y0);
        y_max = std::max(y_max, e.y0);
    }
    if (y_max - y_min > 0.5f || query->x1 - query->x0 < query_least - 0.5f || order->x1 - order->x0 < sort_least - 0.5f) {
        std::fprintf(stderr,
            "  hierarchy shelf at %.0fx%.0f: rows span %.1f px, search %.1f wide (least %.1f), order %.1f wide (least %.1f)\n",
            min.w,
            min.h,
            y_max - y_min,
            query->x1 - query->x0,
            query_least,
            order->x1 - order->x0,
            sort_least);
    }
    RV_EDITOR_CHECK(y_max - y_min <= 0.5f);
    RV_EDITOR_CHECK(query->x1 - query->x0 >= query_least - 0.5f);
    RV_EDITOR_CHECK(order->x1 - order->x0 >= sort_least - 0.5f);
    const std::string bad = rv_editor_fits_whole(row, ImRect(0, 0, min.w, min.h));
    if (!bad.empty()) {
        std::fprintf(stderr, "  hierarchy shelf: %s\n", bad.c_str());
    }
    RV_EDITOR_CHECK(bad.empty());
}
