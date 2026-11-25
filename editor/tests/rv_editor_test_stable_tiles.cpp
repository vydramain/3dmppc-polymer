// A tile relayouts only when it is resized, never by state: buttons whose label changes keep one width, the
// transport bar and the Release strip choose their tile size by width alone, a strip's minimum is constant.

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_pane_controls.hpp"
#include "panes/rv_editor_release_facts.hpp"
#include "platform/rv_editor_process.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "rv_editor_test_stable_tiles.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::stable_tiles;

RV_EDITOR_TEST(rv_editor_test_button_width_for_is_the_widest_label)
{
    rv_editor_test_imgui gui;
    gui.frame([&] {
        const float run = rv_editor_button_width("Run");
        const float resume = rv_editor_button_width("Resume");
        RV_EDITOR_CHECK(resume > run);
        RV_EDITOR_CHECK(rv_editor_button_width_for({ "Run", "Resume" }) == resume);
        RV_EDITOR_CHECK(rv_editor_button_width_for({ "Resume", "Run" }) == resume);
        RV_EDITOR_CHECK(rv_editor_button_width_for({ "Run" }) == run);
    });
}

RV_EDITOR_TEST(rv_editor_test_fixed_button_run_resume_same_rect)
{
    rv_editor_test_imgui gui;
    const ImVec2 run = fixed_size(gui, "Run", { "Run", "Resume" });
    const ImVec2 resume = fixed_size(gui, "Resume", { "Run", "Resume" });
    RV_EDITOR_CHECK(run.x == resume.x);
    RV_EDITOR_CHECK(run.y == resume.y);
    RV_EDITOR_CHECK(run.x > 0.0f);
}

RV_EDITOR_TEST(rv_editor_test_fixed_button_stop_kill_and_reload_restart_same_rect)
{
    rv_editor_test_imgui gui;
    RV_EDITOR_CHECK(fixed_size(gui, "Stop", { "Stop", "Kill" }).x == fixed_size(gui, "Kill", { "Stop", "Kill" }).x);
    RV_EDITOR_CHECK(
        fixed_size(gui, "Reload", { "Reload", "Restart" }).x == fixed_size(gui, "Restart", { "Reload", "Restart" }).x);
}

RV_EDITOR_TEST(rv_editor_test_fixed_button_is_as_wide_as_width_for)
{
    rv_editor_test_imgui gui;
    float expected = 0.0f;
    gui.frame([&] {
        expected = rv_editor_button_width_for({ "Output Source", "Level" });
    });
    RV_EDITOR_CHECK(fixed_size(gui, "Level", { "Output Source", "Level" }).x == expected);
}

// The action slot is always reserved: its width is that of the widest action, so it does not follow the state.
RV_EDITOR_TEST(rv_editor_test_controls_action_slot_width_covers_every_action)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    gui.frame([&] {
        const float slot = rv_editor_controls_action_slot_width(theme);
        RV_EDITOR_CHECK(slot >= rv_editor_button_width(rv_editor_text("panes.force_stop_button")));
        RV_EDITOR_CHECK(slot >= rv_editor_button_width(rv_editor_text("panes.cancel_build_button")));
        for (const char *key : { "panes.reload_accepted",
                 "panes.reload_refused",
                 "panes.reloading",
                 "panes.reload_on_save",
                 "panes.bake_failed" }) {
            for (const auto kind : { rv_editor_status_kind::ok, rv_editor_status_kind::error, rv_editor_status_kind::active }) {
                RV_EDITOR_CHECK(slot >= rv_editor_status_width(rv_editor_text(key), kind, theme));
            }
        }
    });
}

// One width, one reserve: no state of the machine moves or resizes a button of the bar.
RV_EDITOR_TEST(rv_editor_test_transport_bar_same_rects_in_every_state_at_one_width)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    for (float width = 200.0f; width <= 900.0f; width += 25.0f) {
        float reserve = 0.0f;
        gui.frame(
            [&] {
                reserve = rv_editor_controls_action_slot_width(theme);
            },
            width);
        const auto draw = [&](bool resume, bool build_restart) {
            return rv_editor_probe_draw(gui, width, [&](rv_editor_probe_record &) {
                rv_editor_transport_bar(transport_state(resume, build_restart), theme, reserve);
            });
        };
        const rv_editor_probe_record base = draw(false, false);
        RV_EDITOR_CHECK(same_rects(base, draw(true, false)).empty());
        RV_EDITOR_CHECK(same_rects(base, draw(false, true)).empty());
        RV_EDITOR_CHECK(same_rects(base, draw(true, true)).empty());
    }
}

// The widths may reflow; the old width gives the old rectangles back.
RV_EDITOR_TEST(rv_editor_test_transport_bar_returns_to_old_rects_at_old_width)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    const auto r = rv_editor_probe_resize(gui, 700.0f, 300.0f, [&](rv_editor_probe_record &) {
        rv_editor_transport_bar(transport_state(false, false), theme, 0.0f);
    });
    RV_EDITOR_CHECK(r.difference.empty());
    RV_EDITOR_CHECK(!r.first.entries.empty());
}

// The size is chosen by width alone, from the widest label, never from the label shown.
RV_EDITOR_TEST(rv_editor_test_transport_bar_run_and_resume_take_one_width)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    for (float width = 200.0f; width <= 900.0f; width += 25.0f) {
        const auto run = rv_editor_probe_draw(gui, width, [&](rv_editor_probe_record &) {
            rv_editor_transport_bar(transport_state(false, false), theme, 0.0f);
        });
        const auto resume = rv_editor_probe_draw(gui, width, [&](rv_editor_probe_record &) {
            rv_editor_transport_bar(transport_state(true, false), theme, 0.0f);
        });
        RV_EDITOR_CHECK(same_rects(run, resume).empty());
    }
}

// The minimum of a strip is the pane kind's, not what a state made it draw.
RV_EDITOR_TEST(rv_editor_test_strip_minimum_does_not_follow_the_drawn_height)
{
    rv_editor_test_imgui gui;
    gui.frame([&] {
        for (const auto kind :
            { rv_editor_pane_kind::controls, rv_editor_pane_kind::release_controls, rv_editor_pane_kind::toolchest }) {
            rv_editor_shell a, b, none;
            rv_editor_shell_set_minimum(a, 7, kind, { 0, 0 });
            rv_editor_shell_set_minimum(b, 7, kind, { 0, 0 });
            rv_editor_shell_set_minimum(none, 7, kind, { 0, 0 });
            RV_EDITOR_CHECK(a.ws.minimums.count(7) == 1);
            RV_EDITOR_CHECK(b.ws.minimums.count(7) == 1);
            RV_EDITOR_CHECK(none.ws.minimums.count(7) == 1);
            if (a.ws.minimums.count(7) == 1 && b.ws.minimums.count(7) == 1) {
                RV_EDITOR_CHECK(a.ws.minimums[7].w == b.ws.minimums[7].w);
                RV_EDITOR_CHECK(a.ws.minimums[7].h == b.ws.minimums[7].h);
                RV_EDITOR_CHECK(a.ws.minimums[7].h > 0);
            }
            if (none.ws.minimums.count(7) == 1 && a.ws.minimums.count(7) == 1) {
                RV_EDITOR_CHECK(none.ws.minimums[7].h == a.ws.minimums[7].h);
            }
        }
    });
}

// The shell keeps no drawn strip heights any more: a minimum is the pane kind's alone.
template <class T>
constexpr bool has_strips = requires(T s) { s.strips; };

RV_EDITOR_TEST(rv_editor_test_shell_has_no_strips_member)
{
    RV_EDITOR_CHECK(!has_strips<rv_editor_shell>);
}

RV_EDITOR_TEST(rv_editor_test_game_minimum_does_not_follow_its_status)
{
    rv_editor_test_imgui gui;
    gui.frame([&] {
        rv_editor_shell a, b;
        rv_editor_shell_set_minimum(a, 3, rv_editor_pane_kind::game, { 200, 40 });
        rv_editor_shell_set_minimum(b, 3, rv_editor_pane_kind::game, { 500, 120 });
        RV_EDITOR_CHECK(a.ws.minimums.count(3) == 1);
        RV_EDITOR_CHECK(b.ws.minimums.count(3) == 1);
        if (a.ws.minimums.count(3) == 1 && b.ws.minimums.count(3) == 1) {
            RV_EDITOR_CHECK(a.ws.minimums[3].w == b.ws.minimums[3].w);
            RV_EDITOR_CHECK(a.ws.minimums[3].h == b.ws.minimums[3].h);
        }
    });
}
