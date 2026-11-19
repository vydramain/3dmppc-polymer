// Runtime Controls, Release Controls, Toolchest and the Game shelf keep their places in every state.

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

// Runtime Controls is the transport alone: the profile is chosen in Run Config and the facts line is gone from this
// strip (the approved transport pattern), so neither the profile button nor the facts line is drawn here.
RV_EDITOR_TEST(rv_editor_test_controls_has_no_profile_button_and_no_facts)
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
    const auto r = draw_pane(gui, shell, rv_editor_pane_kind::controls, min.w);
    RV_EDITOR_CHECK(entry(r, "##facts") == nullptr);
    for (const auto &e : r.entries) {
        RV_EDITOR_CHECK(e.label.find("a-profile-with-a-rather-long-name") == std::string::npos);
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
