// S14, R14, AC15: the tile of Runtime Controls and of Release Controls is as high as its strip at the tile's current width,
// in every layout (untouched or not, the strip above or below its neighbour), recomputed when the width changes, and a
// splitter dragged next to it does not change that height. Game: Run and the picture keep their rectangles in every state.

#include <cmath>
#include <cstdio>
#include <string>

#include "rv_editor_test.hpp"
#include "rv_editor_test_s14.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s14;

namespace
{

// What a strip tile may keep free under its content: the spacing the content ends with, and a pixel of rounding.
float empty_allowed()
{
    return ImGui::GetStyle().ItemSpacing.y + 1.5f;
}

float allowed_now(rv_editor_test_imgui &gui)
{
    float v = 0.0f;
    gui.frame([&] {
        v = empty_allowed();
    });
    return v;
}

// "" when the strip's body at this moment holds its content exactly: nothing scrolls, nothing is left empty under it.
std::string body_is_the_strip(rv_editor_test_imgui &gui, const tiles &t)
{
    const float over = t.s.overflow.y;
    std::string bad = body_is_scroll_area(t.record) ? "the tile body is a scroll window; " : "";
    if (over > 0.5f) {
        bad += "content past the body by " + std::to_string(over) + " px (body " + std::to_string(t.s.size.y) + ")";
    } else if (-over > allowed_now(gui)) {
        bad += std::to_string(-over) + " px empty under the content (body " + std::to_string(t.s.size.y) + ")";
    }
    return bad;
}

} // namespace

// AC15, R14: at every width the tile's body is as high as the strip's content, in untouched and in touched layouts, with the
// strip first or second, and it is recomputed when the window gets wider and when it gets narrower again.
RV_EDITOR_TEST(rv_editor_test_s14_strip_height_follows_the_width_in_every_layout)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : { rv_editor_pane_kind::controls, rv_editor_pane_kind::release_controls }) {
        for (const bool untouched : { true, false }) {
            for (const bool strip_first : { true, false }) {
                own_shell own;
                tiles t(gui, own.shell, kind, strip_first, rv_editor_tile_dock::bottom, untouched);
                float last_body = 0.0f;
                for (const float width : { 900.0f, 1500.0f, 700.0f, 1100.0f }) {
                    t.settle(width, 700.0f);
                    const std::string bad = body_is_the_strip(gui, t);
                    if (!bad.empty()) {
                        std::fprintf(stderr,
                            "  %s untouched=%d strip_first=%d window %.0f: %s\n",
                            name_of(kind),
                            untouched,
                            strip_first,
                            width,
                            bad.c_str());
                    }
                    RV_EDITOR_CHECK(bad.empty());
                    last_body = t.s.size.y;
                }
                RV_EDITOR_CHECK(last_body > 0.0f);
            }
        }
    }
}

// AC15: a splitter dragged next to a strip is let go and the strip is as high as before.
RV_EDITOR_TEST(rv_editor_test_s14_a_splitter_does_not_change_the_strip_height)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : { rv_editor_pane_kind::controls, rv_editor_pane_kind::release_controls }) {
        for (const bool strip_first : { true, false }) {
            for (const bool untouched : { true, false }) {
                own_shell own;
                tiles t(gui, own.shell, kind, strip_first, rv_editor_tile_dock::bottom, untouched);
                t.settle(1000.0f, 700.0f);
                const float before = t.s.size.y;
                RV_EDITOR_CHECK(before > 0.0f);
                for (const float ratio : { 0.9f, 0.1f, 0.5f }) {
                    RV_EDITOR_CHECK(rv_editor_tile_set_ratio(own.shell.ws.layout, t.split(), ratio) == RV_OK);
                    own.shell.ws.dragged = true;
                    t.settle(1000.0f, 700.0f);
                    if (std::fabs(t.s.size.y - before) > 1.0f) {
                        std::fprintf(stderr,
                            "  %s strip_first=%d untouched=%d ratio %.1f: strip body %.1f, before the drag %.1f\n",
                            name_of(kind),
                            strip_first,
                            untouched,
                            ratio,
                            t.s.size.y,
                            before);
                    }
                    RV_EDITOR_CHECK(std::fabs(t.s.size.y - before) <= 1.0f);
                }
            }
        }
    }
}

// R14: Run is in the Game shelf in every state with a project, and the shelf and the picture area keep their rectangles
// whatever the status says (empty, long, a note), at the minimum width and wider.
RV_EDITOR_TEST(rv_editor_test_s14_game_keeps_run_and_the_picture_in_every_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(rv_editor_pane_kind::game);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::game);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    const std::string run = rv_editor_text("pane_game.run");
    for (const float width : { min.w, 900.0f }) {
        const float height = std::max(min.h, 500.0f);
        const auto base = draw_at(gui, *all.shells[all.project_from], rv_editor_pane_kind::game, width, height);
        const auto run_base = [&](const rv_editor_probe_record &r) {
            for (const auto &e : r.entries) {
                if (e.label == run) {
                    return &e;
                }
            }
            return static_cast<const rv_editor_probe_entry *>(nullptr);
        };
        RV_EDITOR_CHECK(run_base(base) != nullptr);
        for (size_t i = all.project_from + 1; i < all.shells.size(); ++i) {
            const auto r = draw_at(gui, *all.shells[i], rv_editor_pane_kind::game, width, height);
            RV_EDITOR_CHECK(run_base(r) != nullptr);
            const std::string d = stable_tiles::same_rects(base, r);
            if (!d.empty()) {
                std::fprintf(stderr, "  game width %.0f, state '%s': %s\n", width, all.names[i].c_str(), d.c_str());
            }
            RV_EDITOR_CHECK(d.empty());
        }
    }
}

// R14, R26: in a layout the user touched a strip beside another pane (an x split) keeps the width the user gave it: only
// the strip's height is fitted there, so the split's share stays whatever the user left.
RV_EDITOR_TEST(rv_editor_test_s14_a_touched_strip_beside_a_pane_keeps_its_width)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : { rv_editor_pane_kind::controls, rv_editor_pane_kind::release_controls }) {
        const float strip_min = stable_tiles::pane_minimum(gui, kind).w;
        for (const bool strip_first : { true, false }) {
            own_shell own;
            tiles t(gui,
                own.shell,
                kind,
                strip_first,
                strip_first ? rv_editor_tile_dock::right : rv_editor_tile_dock::left,
                false);
            // The strip is the first (left) child in both orders, so the ratio is its share in both.
            const float share = 0.7f;
            RV_EDITOR_CHECK(rv_editor_tile_set_ratio(own.shell.ws.layout, t.split(), share) == RV_OK);
            own.shell.ws.dragged = true;
            t.settle(1500.0f, 700.0f);
            const float width = t.s.size.x;
            // Well above the strip's own minimum: the share is not pinned to the minimum.
            RV_EDITOR_CHECK(width > strip_min + 100.0f);
            t.settle(1500.0f, 700.0f, 6);
            const float after = own.shell.ws.layout.nodes[t.split()].split.ratio;
            if (std::fabs(after - share) > 0.001f || std::fabs(t.s.size.x - width) > 1.0f) {
                std::fprintf(stderr,
                    "  %s strip_first=%d: share %.3f became %.3f, strip body width %.1f became %.1f\n",
                    name_of(kind),
                    strip_first,
                    share,
                    after,
                    width,
                    t.s.size.x);
            }
            RV_EDITOR_CHECK(std::fabs(after - share) <= 0.001f);
            RV_EDITOR_CHECK(std::fabs(t.s.size.x - width) <= 1.0f);
        }
    }
}
