// Run Config and Project keep their blocks in place by state and show paths in full: probe pairs (one width, two
// states). (The label, checkbox and button-wrapping checks of Run Config moved to the S15 tests: the tile has a minimum
// now and the form is the S6 form.)

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "rv_editor_test_panes_stable.hpp"
#include "rv_editor_test_strip.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::panes_stable;

// AC8: outside the project root the fields show the whole path.
RV_EDITOR_TEST(rv_editor_test_run_config_paths_show_in_full)
{
    load_texts();
    rv_editor_test_imgui gui;
    path_world world("run_tail");
    const fs::path other = make_dir("run_tail_root");
    rv_editor_app app;
    run_open(app, other);
    run_set_paths(app, world);
    check_paths(gui, app);
    fs::remove_all(other);
}

// AC8: inside the project root the fields show the path relative to it, so the root's own head is not drawn.
RV_EDITOR_TEST(rv_editor_test_run_config_paths_inside_the_root_are_relative)
{
    load_texts();
    rv_editor_test_imgui gui;
    path_world world("run_rel");
    rv_editor_app app;
    run_open(app, world.inner);
    run_set_paths(app, world);
    check_paths(gui, app, world.inner.string());
}

// AC9, R2: a profile error and a problem of the edited profile are a strip under the area: the form's top and the buttons stay,
// the area gives up its bottom.
RV_EDITOR_TEST(rv_editor_test_run_config_error_and_problem_keep_the_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    run_open(app, "/tmp");
    float gap = 0.0f;
    float frame_h = 0.0f;
    gui.frame([&] {
        gap = ImGui::GetStyle().ItemSpacing.y;
        frame_h = ImGui::GetFrameHeight();
    });
    const ImVec2 huge(1.0e6f, 1.0e6f);
    run_draw(gui, app, width);
    const rv_editor_probe_record clean = run_draw(gui, app, width);
    RV_EDITOR_CHECK(!strip::has_message_window(clean));
    app.run_config.error = "project.toml: " + long_words();
    const std::string first = strip::strip_problems(&clean, run_draw(gui, app, width), huge, gap, frame_h);
    if (!first.empty()) {
        std::fprintf(stderr, "  run_config error: %s\n", first.c_str());
    }
    RV_EDITOR_CHECK(first.empty());
    app.run_config.error.clear();
    app.run_config.profiles[0].runtime = "/nonexistent/" + std::string(80, 'r');
    const std::string bad = strip::strip_problems(&clean, run_draw(gui, app, width), huge, gap, frame_h);
    if (!bad.empty()) {
        std::fprintf(stderr, "  run_config problem: %s\n", bad.c_str());
    }
    RV_EDITOR_CHECK(bad.empty());
}

// --- Project ------------------------------------------------------------------------------------------------------

namespace
{

rv_editor_probe_record project_draw(rv_editor_test_imgui &gui, rv_editor_app &app)
{
    return rv_editor_probe_draw(
        gui,
        width,
        [&](rv_editor_probe_record &) {
            rv_editor_pane_project(app, theme);
        },
        tall);
}

void project_open(rv_editor_app &app)
{
    app.project.open = true;
    app.project.root = "/tmp/rv_s13_project";
    app.project.disc_id = "disc";
}

} // namespace

// AC9, R2: a manifest error, a tool's problem and a settings error are a strip under the areas and move nothing above it.
RV_EDITOR_TEST(rv_editor_test_project_error_lines_keep_the_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    project_open(app);
    float gap = 0.0f;
    float frame_h = 0.0f;
    gui.frame([&] {
        gap = ImGui::GetStyle().ItemSpacing.y;
        frame_h = ImGui::GetFrameHeight();
    });
    const ImVec2 huge(1.0e6f, 1.0e6f);
    project_draw(gui, app);
    const rv_editor_probe_record clean = project_draw(gui, app);
    RV_EDITOR_CHECK(!strip::has_message_window(clean));
    app.project.manifest_error = "disc.toml:3: " + long_words();
    RV_EDITOR_CHECK(strip::strip_problems(&clean, project_draw(gui, app), huge, gap, frame_h).empty());
    app.project.manifest_error.clear();
    app.tools.burner.problem = "cannot run the burner: " + long_words();
    RV_EDITOR_CHECK(strip::strip_problems(&clean, project_draw(gui, app), huge, gap, frame_h).empty());
    app.tools.burner.problem.clear();
    app.tools.settings_error = "settings.toml:2: " + long_words();
    RV_EDITOR_CHECK(strip::strip_problems(&clean, project_draw(gui, app), huge, gap, frame_h).empty());
}
