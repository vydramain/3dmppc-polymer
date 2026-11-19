// Run Config and Project keep their blocks in place by state and show paths in full: probe pairs (one width, two
// states). (The label, checkbox and button-wrapping checks of Run Config moved to the S15 tests: the tile has a minimum
// now and the form is the S6 form.)

#include <unistd.h>

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

// AC9: a profile error, a problem of the edited profile do not move the form or the buttons.
RV_EDITOR_TEST(rv_editor_test_run_config_error_and_problem_keep_the_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    run_open(app, "/tmp");
    run_draw(gui, app, width);
    const rv_editor_probe_record clean = run_draw(gui, app, width);
    const std::vector<std::string> anchors = { "##Name", rv_editor_text("pane_run_config.apply_label") };
    app.run_config.error = "project.toml: " + long_words();
    const rv_editor_probe_record with_error = run_draw(gui, app, width);
    RV_EDITOR_CHECK(stable(outside_areas(clean, anchors), outside_areas(with_error, anchors), anchors).empty());
    app.run_config.error.clear();
    app.run_config.profiles[0].runtime = "/nonexistent/" + std::string(80, 'r');
    const rv_editor_probe_record with_problem = run_draw(gui, app, width);
    RV_EDITOR_CHECK(stable(outside_areas(clean, anchors), outside_areas(with_problem, anchors), anchors).empty());
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

// AC9: a manifest error, a tool's problem and a settings error do not move what follows them.
RV_EDITOR_TEST(rv_editor_test_project_error_lines_keep_the_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_app app;
    project_open(app);
    project_draw(gui, app);
    const rv_editor_probe_record clean = project_draw(gui, app);
    const std::vector<std::string> anchors = { rv_editor_text("panes.look_for_tools_button") };
    app.project.manifest_error = "disc.toml:3: " + long_words();
    RV_EDITOR_CHECK(stable(clean, project_draw(gui, app), anchors).empty());
    app.project.manifest_error.clear();
    app.tools.burner.problem = "cannot run the burner: " + long_words();
    RV_EDITOR_CHECK(stable(clean, project_draw(gui, app), anchors).empty());
    app.tools.burner.problem.clear();
    app.tools.settings_error = "settings.toml:2: " + long_words();
    RV_EDITOR_CHECK(stable(clean, project_draw(gui, app), anchors).empty());
}
