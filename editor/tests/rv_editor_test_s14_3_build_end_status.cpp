// S14.3 (live run): after a build that failed at a step, the status under the buttons of Runtime Controls and the build
// field of the status bar say how the build ended ("Build #N failed"), not the step it stopped at ("step 1/5: sources").

#include <string>

#include "app/rv_editor_shell.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s14.hpp"
#include "ui/rv_editor_widgets_strip_status.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s14;

namespace
{

// A shell whose build ran the burner's first step and failed there, as the live run of solid-maid did.
struct failed_at_a_step {
    fs::path where = pane_state::fresh_dir("rv_editor_s14_3");
    rv_editor_shell shell;
    failed_at_a_step()
    {
        pane_state::open_project(shell.app, where);
        script_build(shell.app,
            where,
            "echo 'mppcburner: [plan] sources manifest compile assets burn' >&2\n"
            "echo 'mppcburner: [1/5] sources x' >&2\n"
            "echo 'mppcburner: [end] failed: sources have error findings' >&2\n"
            "exit 1\n");
        std::string error;
        for (int i = 0; i < 300 && shell.app.build.busy(); ++i) {
            shell.app.build.update(shell.app.log, error);
            usleep(10000);
        }
    }
    ~failed_at_a_step()
    {
        pane_state::remove_dir(where);
    }
};

} // namespace

// AC12: Runtime Controls, one line: "<run state> | Build #N failed".
RV_EDITOR_TEST(rv_editor_test_s14_3_controls_status_says_the_build_failed)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    failed_at_a_step f;
    RV_EDITOR_CHECK(f.shell.app.build.state() == rv_editor_build_state::failed);
    RV_EDITOR_CHECK(!f.shell.app.build.progress().runs.empty()); // the step is known: it is not the "no step" case
    (void)draw_at(gui, f.shell, rv_editor_pane_kind::controls, 1000.0f, 400.0f);
    const std::string text = rv_editor_strip_status_last();
    RV_EDITOR_CHECK(text == "Stopped | Build #" + std::to_string(f.shell.app.build.number()) + " failed");
}

// AC12: the status bar's build field says the same.
RV_EDITOR_TEST(rv_editor_test_s14_3_status_bar_says_the_build_failed)
{
    load_texts();
    failed_at_a_step f;
    RV_EDITOR_CHECK(f.shell.app.build.state() == rv_editor_build_state::failed);
    RV_EDITOR_CHECK(
        rv_editor_status_text_make(f.shell.app).build == "Build #" + std::to_string(f.shell.app.build.number()) + " failed");
}
