// A tile relayouts only when it is resized, never by state: buttons whose label changes keep one width, the
// transport bar and the Release strip choose their tile size by width alone, a strip's minimum is constant.

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
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

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;

// The size of the item just drawn.
ImVec2 item_size()
{
    return ImVec2(ImGui::GetItemRectMax().x - ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y - ImGui::GetItemRectMin().y);
}

// "" when both draws have the same rectangles in order (ids differ with a label, so they are not matched).
std::string same_rects(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    if (a.entries.size() != b.entries.size()) {
        return "item count " + std::to_string(a.entries.size()) + " became " + std::to_string(b.entries.size());
    }
    for (size_t i = 0; i < a.entries.size(); ++i) {
        const auto &x = a.entries[i];
        const auto &y = b.entries[i];
        if (x.x0 != y.x0 || x.y0 != y.y0 || x.x1 != y.x1 || x.y1 != y.y1) {
            return "item " + std::to_string(i) + " " + rv_editor_probe_detail::rect(x) + " became " +
                rv_editor_probe_detail::rect(y);
        }
    }
    return a.content_height == b.content_height ? "" : "content height changed";
}

rv_editor_transport_state transport_state(bool resume, bool build_restart)
{
    rv_editor_transport_state s = {};
    s.resume = resume;
    s.build_restart = build_restart;
    return s;
}

// What a fixed button of `label` among `all` occupies.
ImVec2 fixed_size(rv_editor_test_imgui &gui, const char *label, std::initializer_list<const char *> all)
{
    ImVec2 size;
    gui.frame([&] {
        rv_editor_button_fixed(label, all, theme);
        size = item_size();
    });
    return size;
}

} // namespace

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

RV_EDITOR_TEST(rv_editor_test_release_tile_size_by_width_alone)
{
    rv_editor_test_imgui gui;
    gui.frame([&] {
        const rv_editor_tile tiles[5] = { { "Build", "B" },
            { "Run", "R" },
            { "Player", "P" },
            { "Stop", "S" },
            { "Report", "X" } };
        RV_EDITOR_CHECK(rv_editor_release_tile_size(tiles, 4000.0f) == rv_editor_tile_size::large);
        RV_EDITOR_CHECK(rv_editor_release_tile_size(tiles, 10.0f) == rv_editor_tile_size::small);
        // Narrower never gives a larger tile.
        int last = 0;
        for (float avail = 4000.0f; avail >= 0.0f; avail -= 10.0f) {
            const int size = static_cast<int>(rv_editor_release_tile_size(tiles, avail));
            RV_EDITOR_CHECK(size >= last);
            last = size;
        }
        // Large is reached at one fixed width, whatever the state's text: it does not move with the facts.
        float need = 0.0f;
        for (float avail = 0.0f; avail <= 4000.0f; avail += 1.0f) {
            if (rv_editor_release_tile_size(tiles, avail) == rv_editor_tile_size::large) {
                need = avail;
                break;
            }
        }
        RV_EDITOR_CHECK(need > 0.0f);
    });
}

// The minimum of a strip is the pane kind's, not what a state made it draw.
RV_EDITOR_TEST(rv_editor_test_strip_minimum_does_not_follow_the_drawn_height)
{
    rv_editor_test_imgui gui;
    gui.frame([&] {
        for (const auto kind :
            { rv_editor_pane_kind::controls, rv_editor_pane_kind::release_controls, rv_editor_pane_kind::toolchest }) {
            rv_editor_shell a, b, none;
            a.strips[7] = { 400, 60 };
            b.strips[7] = { 400, 140 };
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

namespace
{

namespace fs = std::filesystem;

// An open project; build.start needs a root and a cache dir, which `where` gives it.
void open_project(rv_editor_app &app, const fs::path &where)
{
    app.project.open = true;
    app.project.root = where;
    app.project.disc_id = "disc";
    app.project.cache_dir = where / "cache";
}

// A burner stand-in that outlives the test's frame; the process is killed with the build.
void start_busy_build(rv_editor_app &app, const fs::path &where)
{
    // The process guard starts once per process; another test may have started it, so the result is not checked.
    std::string guard_error;
    rv_editor_guard_start(guard_error);
    const fs::path script = where / "burner.sh";
    std::ofstream(script) << "#!/bin/sh\nsleep 30\n";
    chmod(script.c_str(), 0700);
    app.tools.burner.path = script;
    app.tools.baker.path = script;
    std::string error;
    RV_EDITOR_CHECK(app.build.start(app.project, app.tools, app.log, error) == RV_OK);
    RV_EDITOR_CHECK(app.build.busy());
}

// The height a pane body draws at `width`, from the first cursor row to the last, in the shell's own pane dispatch.
float drawn_height(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind, float width)
{
    float top = 0.0f;
    const rv_editor_probe_record record = rv_editor_probe_draw(gui, width, [&](rv_editor_probe_record &) {
        top = ImGui::GetCursorPosY();
        rv_editor_shell_pane(&shell, 1, kind, theme);
    });
    return record.content_height - top;
}

struct minimum {
    float w, h;
};

// The constant minimum of `kind`, set the way the shell sets it (inside a frame: it measures the font).
minimum pane_minimum(rv_editor_test_imgui &gui, rv_editor_pane_kind kind)
{
    minimum result{ 0, 0 };
    gui.frame([&] {
        rv_editor_shell shell;
        rv_editor_shell_set_minimum(shell, 1, kind, { 0, 0 });
        const auto it = shell.ws.minimums.find(1);
        if (it != shell.ws.minimums.end()) {
            result = { static_cast<float>(it->second.w), static_cast<float>(it->second.h) };
        }
    });
    return result;
}

// What the tile draws at its minimum width in one state must fit its constant minimum height, which is the same
// in every state: a minimum under what the tile draws lets the tile clip its content.
void check_fits(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind, const char *state)
{
    const minimum min = pane_minimum(gui, kind);
    RV_EDITOR_CHECK(min.h > 0.0f);
    const float drawn = drawn_height(gui, shell, kind, min.w);
    if (drawn > min.h) {
        std::fprintf(stderr, "  %s: drawn %.1f > minimum %.1f at width %.1f\n", state, drawn, min.h, min.w);
    }
    RV_EDITOR_CHECK(drawn <= min.h);
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_toolchest_fits_its_minimum_in_every_state)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    // The debug mark row, with and without a mark.
    rv_editor_shell debug;
    debug.app.preset = rv_editor_layout_preset::debug;
    check_fits(gui, debug, rv_editor_pane_kind::toolchest, "debug, no mark");
    debug.app.marks.push_back("m");
    check_fits(gui, debug, rv_editor_pane_kind::toolchest, "debug, a short mark");
    debug.app.marks.push_back("a mark with a rather long description of the moment it names");
    check_fits(gui, debug, rv_editor_pane_kind::toolchest, "debug, a mark");
    // The scene document's tools.
    rv_editor_shell scene;
    scene.app.preset = rv_editor_layout_preset::scene;
    scene.app.scene = std::make_unique<rv_editor_scene_doc>();
    check_fits(gui, scene, rv_editor_pane_kind::toolchest, "scene tools");
    // No scene: the wrapped line.
    rv_editor_shell none;
    none.app.preset = rv_editor_layout_preset::scene;
    check_fits(gui, none, rv_editor_pane_kind::toolchest, "no scene");
}

RV_EDITOR_TEST(rv_editor_test_controls_fits_its_minimum_in_every_state)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    const fs::path where = fs::temp_directory_path() / "rv_editor_stable_controls";
    std::error_code ec;
    fs::remove_all(where, ec);
    fs::create_directories(where, ec);
    {
        rv_editor_shell shell;
        open_project(shell.app, where);
        check_fits(gui, shell, rv_editor_pane_kind::controls, "idle");
        // A running build puts Cancel Build in the action slot.
        start_busy_build(shell.app, where);
        check_fits(gui, shell, rv_editor_pane_kind::controls, "Cancel Build");
    }
    fs::remove_all(where, ec);
}

RV_EDITOR_TEST(rv_editor_test_release_controls_fits_its_minimum_in_every_state)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    const fs::path where = fs::temp_directory_path() / "rv_editor_stable_release";
    std::error_code ec;
    fs::remove_all(where, ec);
    fs::create_directories(where, ec);
    {
        rv_editor_shell shell;
        open_project(shell.app, where);
        check_fits(gui, shell, rv_editor_pane_kind::release_controls, "no candidate");
        shell.app.release.candidates.emplace_back();
        shell.app.release.candidates.back().number = 3;
        check_fits(gui, shell, rv_editor_pane_kind::release_controls, "a candidate");
        start_busy_build(shell.app, where);
        check_fits(gui, shell, rv_editor_pane_kind::release_controls, "building");
    }
    fs::remove_all(where, ec);
}

// The Game's picture fills the well under its mode row and the status line is drawn over it, so the tile needs what
// is around the picture plus one status row: measured in a tall window as its height less the picture's.
RV_EDITOR_TEST(rv_editor_test_game_fits_its_minimum_stopped)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    const minimum min = pane_minimum(gui, rv_editor_pane_kind::game);
    RV_EDITOR_CHECK(min.h > 0.0f);
    float status_row = 0.0f, window_pad = 0.0f;
    gui.frame([&] {
        status_row = ImGui::GetTextLineHeightWithSpacing();
        window_pad = ImGui::GetStyle().WindowPadding.y;
    });
    rv_editor_shell shell;
    shell.app.project.open = true;
    const float height = 400.0f;
    for (const float width : { 300.0f, 800.0f }) {
        rv_editor_probe_record record;
        rv_editor_probe_detail::sink sink(&record);
        rv_editor_item_sink_set(&sink);
        ImGui::GetCurrentContext()->TestEngineHookItems = true;
        gui.frame(
            [&] {
                rv_editor_shell_pane(&shell, 1, rv_editor_pane_kind::game, theme);
            },
            width,
            height);
        ImGui::GetCurrentContext()->TestEngineHookItems = false;
        rv_editor_item_sink_set(nullptr);
        // The picture's Dummy is the last item the pane submits.
        RV_EDITOR_CHECK(!record.entries.empty());
        if (!record.entries.empty()) {
            const auto &picture = record.entries.back();
            const float around = (height - 2.0f * window_pad) - (picture.y1 - picture.y0);
            if (around + status_row > min.h) {
                std::fprintf(stderr, "  game w %.0f: needs %.1f > minimum %.1f\n", width, around + status_row, min.h);
            }
            RV_EDITOR_CHECK(around + status_row <= min.h);
        }
    }
}
