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

// --- S10: Runtime Controls, Release Controls, Toolchest and the Game shelf keep their places in every state ---------

namespace
{

// The pane drawn twice at `width` (the second draw is the settled one), in the shell's own dispatch.
rv_editor_probe_record draw_pane(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind, float width)
{
    const auto body = [&](rv_editor_probe_record &) {
        rv_editor_shell_pane(&shell, 1, kind, theme);
    };
    rv_editor_probe_draw(gui, width, body);
    return rv_editor_probe_draw(gui, width, body);
}

const rv_editor_probe_entry *entry(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label == label) {
            return &e;
        }
    }
    return nullptr;
}

// True when a tooltip shows with the mouse over the item `label`, found in the pane's own draw.
bool tooltip_over_item(rv_editor_test_imgui &gui,
    rv_editor_shell &shell,
    rv_editor_pane_kind kind,
    float width,
    const std::string &label)
{
    ImVec2 mouse(1.0f, 1.0f);
    for (int i = 0; i < 4; ++i) {
        ImGui::GetIO().MousePos = mouse;
        const auto r = rv_editor_probe_draw(gui, width, [&](rv_editor_probe_record &) {
            rv_editor_shell_pane(&shell, 1, kind, theme);
        });
        if (const auto *e = entry(r, label)) {
            mouse = ImVec2((e->x0 + e->x1) / 2.0f, (e->y0 + e->y1) / 2.0f);
        }
    }
    ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    const bool shown = tip != nullptr && tip->WasActive;
    ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    return shown;
}

// A burner stand-in that fails at once; the build is updated until it reports failed.
void start_failed_build(rv_editor_app &app, const fs::path &where)
{
    std::string guard_error;
    rv_editor_guard_start(guard_error);
    const fs::path script = where / "failing.sh";
    std::ofstream(script) << "#!/bin/sh\nexit 1\n";
    chmod(script.c_str(), 0700);
    app.tools.burner.path = script;
    app.tools.baker.path = script;
    std::string error;
    RV_EDITOR_CHECK(app.build.start(app.project, app.tools, app.log, error) == RV_OK);
    for (int i = 0; i < 200 && app.build.busy(); ++i) {
        app.build.update(app.log, error);
        usleep(10000);
    }
    RV_EDITOR_CHECK(app.build.state() == rv_editor_build_state::failed);
}

struct scratch_dir {
    fs::path path;
    explicit scratch_dir(const char *name)
        : path(fs::temp_directory_path() / name)
    {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~scratch_dir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// "" when `b` has the same items in the same rectangles as `a`.
std::string pane_difference(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    return rv_editor_probe_compare(a, b);
}

} // namespace

// The action slot is a place of its own: no session, a running build and a failed build lay the pane out alike.
RV_EDITOR_TEST(rv_editor_test_controls_layout_same_in_no_session_building_and_failed)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch_dir where("rv_editor_stable_controls_s10");
    for (const float width : { 330.0f, 600.0f, 900.0f }) {
        rv_editor_shell idle;
        open_project(idle.app, where.path);
        const auto base = draw_pane(gui, idle, rv_editor_pane_kind::controls, width);
        {
            rv_editor_shell building;
            open_project(building.app, where.path);
            start_busy_build(building.app, where.path);
            const std::string d = pane_difference(base, draw_pane(gui, building, rv_editor_pane_kind::controls, width));
            if (!d.empty()) {
                std::fprintf(stderr, "  controls building, width %.0f: %s\n", width, d.c_str());
            }
            RV_EDITOR_CHECK(d.empty());
        }
        {
            rv_editor_shell failed;
            open_project(failed.app, where.path);
            start_failed_build(failed.app, where.path);
            const std::string d = pane_difference(base, draw_pane(gui, failed, rv_editor_pane_kind::controls, width));
            if (!d.empty()) {
                std::fprintf(stderr, "  controls failed, width %.0f: %s\n", width, d.c_str());
            }
            RV_EDITOR_CHECK(d.empty());
        }
    }
}

// AC13: Cancel Build is there with nothing to cancel, disabled with its reason as the tooltip, in the slot it keeps
// while a build runs.
RV_EDITOR_TEST(rv_editor_test_controls_cancel_build_exists_in_every_state_in_one_place)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch_dir where("rv_editor_stable_controls_cancel_s10");
    const std::string cancel = rv_editor_text("panes.cancel_build_button");
    rv_editor_shell idle;
    open_project(idle.app, where.path);
    const auto idle_draw = draw_pane(gui, idle, rv_editor_pane_kind::controls, 700.0f);
    const auto *idle_cancel = entry(idle_draw, cancel);
    RV_EDITOR_CHECK(idle_cancel != nullptr);
    RV_EDITOR_CHECK(tooltip_over_item(gui, idle, rv_editor_pane_kind::controls, 700.0f, cancel));
    rv_editor_shell building;
    open_project(building.app, where.path);
    start_busy_build(building.app, where.path);
    const auto busy_draw = draw_pane(gui, building, rv_editor_pane_kind::controls, 700.0f);
    const auto *busy_cancel = entry(busy_draw, cancel);
    RV_EDITOR_CHECK(busy_cancel != nullptr);
    if (idle_cancel != nullptr && busy_cancel != nullptr) {
        RV_EDITOR_CHECK(rv_editor_probe_detail::rect(*idle_cancel) == rv_editor_probe_detail::rect(*busy_cancel));
    }
    rv_editor_shell failed;
    open_project(failed.app, where.path);
    start_failed_build(failed.app, where.path);
    const auto failed_draw = draw_pane(gui, failed, rv_editor_pane_kind::controls, 700.0f);
    const auto *failed_cancel = entry(failed_draw, cancel);
    RV_EDITOR_CHECK(failed_cancel != nullptr);
    if (idle_cancel != nullptr && failed_cancel != nullptr) {
        RV_EDITOR_CHECK(rv_editor_probe_detail::rect(*idle_cancel) == rv_editor_probe_detail::rect(*failed_cancel));
    }
}

// The profile button stays inside the pane at the narrowest widths a tile may take.
RV_EDITOR_TEST(rv_editor_test_controls_profile_button_stays_inside_the_pane)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch_dir where("rv_editor_stable_controls_profile_s10");
    rv_editor_shell shell;
    open_project(shell.app, where.path);
    auto &profiles = shell.app.run_config.profiles;
    profiles[shell.app.run_config.active].name = "a-profile-with-a-rather-long-name-that-does-not-fit";
    const minimum min = pane_minimum(gui, rv_editor_pane_kind::controls);
    const std::string profile = rv_editor_text_format("panes.profile",
        std::make_format_args(shell.app.run_config.profiles[shell.app.run_config.active].name));
    const auto r = draw_pane(gui, shell, rv_editor_pane_kind::controls, min.w);
    const auto *button = entry(r, profile);
    RV_EDITOR_CHECK(button != nullptr);
    if (button != nullptr) {
        if (button->x1 > min.w) {
            std::fprintf(stderr, "  profile button ends at %.1f, pane is %.1f wide\n", button->x1, min.w);
        }
        RV_EDITOR_CHECK(button->x1 <= min.w);
    }
}

RV_EDITOR_TEST(rv_editor_test_release_controls_layout_same_with_and_without_a_build)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch_dir where("rv_editor_stable_release_s10");
    for (const bool candidate : { false, true }) {
        for (const float width : { 500.0f, 700.0f, 1000.0f }) {
            rv_editor_shell idle;
            open_project(idle.app, where.path);
            rv_editor_shell building;
            open_project(building.app, where.path);
            for (rv_editor_shell *shell : { &idle, &building }) {
                if (candidate) {
                    shell->app.release.candidates.emplace_back();
                    shell->app.release.candidates.back().number = 3;
                }
            }
            start_busy_build(building.app, where.path);
            building.app.release.building = true;
            const auto base = draw_pane(gui, idle, rv_editor_pane_kind::release_controls, width);
            const std::string d = pane_difference(base, draw_pane(gui, building, rv_editor_pane_kind::release_controls, width));
            if (!d.empty()) {
                std::fprintf(stderr, "  release candidate=%d building, width %.0f: %s\n", candidate, width, d.c_str());
            }
            RV_EDITOR_CHECK(d.empty());
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_release_controls_cancel_build_exists_without_a_build)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch_dir where("rv_editor_stable_release_cancel_s10");
    const std::string cancel = rv_editor_text("pane_candidate.cancel_build_button");
    rv_editor_shell idle;
    open_project(idle.app, where.path);
    RV_EDITOR_CHECK(entry(draw_pane(gui, idle, rv_editor_pane_kind::release_controls, 800.0f), cancel) != nullptr);
    RV_EDITOR_CHECK(tooltip_over_item(gui, idle, rv_editor_pane_kind::release_controls, 800.0f, cancel));
}

// The Toolchest strip is as tall in the debug mark row (without and with marks, short and long), the scene tools and
// the no-scene text, at one width and at a narrow one.
RV_EDITOR_TEST(rv_editor_test_toolchest_strip_height_same_in_every_state)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    const std::string long_mark(160, 'm');
    for (const float width : { 300.0f, 500.0f, 900.0f }) {
        rv_editor_shell debug;
        debug.app.preset = rv_editor_layout_preset::debug;
        const float none = draw_pane(gui, debug, rv_editor_pane_kind::toolchest, width).content_height;
        debug.app.marks.push_back("m");
        RV_EDITOR_CHECK(draw_pane(gui, debug, rv_editor_pane_kind::toolchest, width).content_height == none);
        debug.app.marks.push_back(long_mark);
        const float long_height = draw_pane(gui, debug, rv_editor_pane_kind::toolchest, width).content_height;
        if (long_height != none) {
            std::fprintf(stderr, "  toolchest width %.0f: no mark %.1f, long mark %.1f\n", width, none, long_height);
        }
        RV_EDITOR_CHECK(long_height == none);
        rv_editor_shell scene;
        scene.app.preset = rv_editor_layout_preset::scene;
        scene.app.scene = std::make_unique<rv_editor_scene_doc>();
        const float scene_height = draw_pane(gui, scene, rv_editor_pane_kind::toolchest, width).content_height;
        if (scene_height != none) {
            std::fprintf(stderr, "  toolchest width %.0f: debug %.1f, scene tools %.1f\n", width, none, scene_height);
        }
        RV_EDITOR_CHECK(scene_height == none);
        rv_editor_shell no_scene;
        no_scene.app.preset = rv_editor_layout_preset::scene;
        const float no_scene_height = draw_pane(gui, no_scene, rv_editor_pane_kind::toolchest, width).content_height;
        if (no_scene_height != none) {
            std::fprintf(stderr, "  toolchest width %.0f: debug %.1f, no scene %.1f\n", width, none, no_scene_height);
        }
        RV_EDITOR_CHECK(no_scene_height == none);
    }
}

// The Run button of the Game shelf is in the Burn view too: in the same place, disabled with a reason.
RV_EDITOR_TEST(rv_editor_test_game_shelf_run_present_in_debug_and_burn)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    const std::string run = rv_editor_text("pane_game.run");
    for (const float width : { 300.0f, 800.0f }) {
        rv_editor_shell debug;
        debug.app.project.open = true;
        rv_editor_shell burn;
        burn.app.project.open = true;
        burn.app.release_view = true;
        const auto debug_draw = draw_pane(gui, debug, rv_editor_pane_kind::game, width);
        const auto burn_draw = draw_pane(gui, burn, rv_editor_pane_kind::game, width);
        const auto *in_debug = entry(debug_draw, run);
        const auto *in_burn = entry(burn_draw, run);
        RV_EDITOR_CHECK(in_debug != nullptr);
        RV_EDITOR_CHECK(in_burn != nullptr);
        if (in_debug != nullptr && in_burn != nullptr) {
            RV_EDITOR_CHECK(rv_editor_probe_detail::rect(*in_debug) == rv_editor_probe_detail::rect(*in_burn));
        }
    }
    rv_editor_shell burn;
    burn.app.project.open = true;
    burn.app.release_view = true;
    RV_EDITOR_CHECK(tooltip_over_item(gui, burn, rv_editor_pane_kind::game, 800.0f, run));
}
