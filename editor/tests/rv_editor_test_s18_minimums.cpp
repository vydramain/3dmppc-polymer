// S18, R1/R3/R6, AC1: the Code tile and Review Changes each have one minimum that no state changes and no drag or
// window size goes under; it holds every ask card of the kind whole, and the body of the tile has no scrolling.

#include <algorithm>
#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s18.hpp"
#include "ui/rv_editor_widgets_content.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s18;

RV_EDITOR_TEST(rv_editor_test_s18_every_kind_has_one_minimum_whatever_the_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        fixture f(gui, kind);
        const auto first = minimum_now(gui, f.shell, kind);
        if (first.w <= 0.0f || first.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own (%.0f x %.0f)\n", name_of(kind), first.w, first.h);
        }
        RV_EDITOR_CHECK(first.w > 0.0f && first.h > 0.0f);
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

// R12: the minimum is at least as big as the biggest ask card of the kind: Unsaved changes, Save As and, for Code, the
// swap file ask; Review Changes holds a content area and the Save As card.
RV_EDITOR_TEST(rv_editor_test_s18_the_minimum_holds_the_ask_cards)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        ImVec2 card{ 0, 0 };
        float content = 0.0f;
        gui.frame([&] {
            for (const auto &buttons : { unsaved_buttons(), save_as_buttons(), swap_buttons() }) {
                const ImVec2 one = rv_editor_mode_card_min_size(buttons, stable_tiles::theme);
                card = { std::max(card.x, one.x), std::max(card.y, one.y) };
            }
            content = rv_editor_content_min_height();
        });
        if (min.w < card.x || min.h < card.y) {
            std::fprintf(stderr,
                "  %s: minimum %.0fx%.0f is under its ask card %.0fx%.0f\n",
                name_of(kind),
                min.w,
                min.h,
                card.x,
                card.y);
        }
        RV_EDITOR_CHECK(min.w >= card.x && min.h >= card.y);
        if (kind == rv_editor_pane_kind::review_changes) {
            RV_EDITOR_CHECK(min.h >= content);
        }
    }
}

// A splitter dragged to either end, or a window smaller than the tree, leaves each tile at least its minimum.
RV_EDITOR_TEST(rv_editor_test_s18_a_drag_or_a_small_window_keeps_each_tile_at_its_minimum)
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

// R3, AC1: the body of the tile (the "##pane" window) is drawn at the tile's minimum in every state and has no content
// past its edges, so it shows no scrollbar.
RV_EDITOR_TEST(rv_editor_test_s18_the_tile_body_never_scrolls_at_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        fixture f(gui, kind);
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
