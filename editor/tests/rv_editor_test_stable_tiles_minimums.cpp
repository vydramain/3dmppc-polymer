// Every tile fits its constant minimum in every state.

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
