// AC10: switching workspaces keeps the current build's target, progress and outcome.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#include "app/rv_editor_shell.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_s14.hpp"
#include "ui/rv_editor_widgets_steps.hpp"
#include "ui/rv_editor_widgets_strip_status.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s14;

namespace
{

struct build_layout {
    fs::path where = pane_state::fresh_dir("rv_editor_build_layout");
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    rv_editor_app &app = shell.app;

    explicit build_layout(const char *origin)
    {
        pane_state::open_project(app, where);
        rv_editor_shell_load_layouts(shell, {}, origin);
        const fs::path burner = where / "burner.sh";
        // Hold the real job at checkpoints until the test advances it or clicks Cancel Build.
        pane_state::write_script(burner,
            "echo 'mppcburner: [plan] sources compile' >&2\n"
            "echo 'mppcburner: [1/2] sources' >&2\n"
            "while [ ! -f compile ]; do sleep 0.01; done\n"
            "echo 'mppcburner: [2/2] compile' >&2\n"
            "while [ ! -f finish ]; do sleep 0.01; done\n"
            "case \"$3\" in\n"
            "  --unpacked) mkdir -p \"$4\" || exit 1;;\n"
            "  -o) echo image > \"$4\" || exit 1;;\n"
            "  *) exit 1;;\n"
            "esac\n");
        app.tools.burner.path = burner;
        app.tools.baker.path = burner;
        std::string error;
        const int guard = rv_editor_guard_start(error);
        RV_EDITOR_CHECK(guard == RV_OK || guard == RV_ERR_BUSY);
        rv_editor_shell_update(shell);
    }

    ~build_layout()
    {
        rv_editor_app_shutdown(app);
        pane_state::remove_dir(where);
    }

    void allow(const char *checkpoint) const
    {
        std::ofstream gate(where / checkpoint);
        RV_EDITOR_CHECK(gate.good());
    }

    template <class Predicate>
    bool await(Predicate ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            rv_editor_shell_update(shell);
            if (ready()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    bool at_step(const char *label)
    {
        return await([&] {
            return app.build.busy() && rv_editor_steps_compact(app.build.progress()).ends_with(label);
        });
    }

    rv_editor_probe_record draw_controls()
    {
        const auto kind = shell.active == rv_editor_layout_preset::burn ? rv_editor_pane_kind::release_controls :
                                                                          rv_editor_pane_kind::controls;
        return draw_at(gui, shell, kind, 1000.0f, 400.0f);
    }

    std::string status()
    {
        (void)draw_controls();
        std::string text = rv_editor_strip_status_last();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }

    void cancel_from_controls()
    {
        const char *key =
            shell.active == rv_editor_layout_preset::burn ? "pane_candidate.cancel_build_button" : "panes.cancel_build_button";
        const auto record = draw_controls();
        const auto *button = pane_state::find_item(record, rv_editor_text(key));
        RV_EDITOR_CHECK(button != nullptr);
        if (button == nullptr) {
            return;
        }
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent((button->x0 + button->x1) / 2.0f, (button->y0 + button->y1) / 2.0f);
        (void)draw_controls();
        io.AddMouseButtonEvent(0, true);
        (void)draw_controls();
        io.AddMouseButtonEvent(0, false);
        (void)draw_controls();
        RV_EDITOR_CHECK(rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cancel_build));
    }
};

void check_build_after_switch(bool candidate, rv_editor_build_state outcome)
{
    const auto origin = candidate ? rv_editor_layout_preset::burn : rv_editor_layout_preset::code;
    const auto destination = candidate ? rv_editor_layout_preset::code : rv_editor_layout_preset::burn;
    build_layout f(candidate ? "burn" : "code");
    RV_EDITOR_CHECK(f.shell.active == origin);

    const fs::path candidates = f.app.project.cache_dir / "candidates";
    const fs::path previous_image = candidates / "40.mppcdisc";
    const fs::path target_image = candidates / "41.mppcdisc";
    if (candidate) {
        // Keep a different candidate selected while the next image is under construction.
        fs::create_directories(candidates);
        std::ofstream(previous_image) << "previous image";
        auto previous = s14::candidate(40, false);
        previous.image = previous_image;
        f.app.release.candidates.push_back(std::move(previous));
    }

    const int started = candidate ? rv_editor_app_build_candidate(f.app) : rv_editor_app_build(f.app);
    RV_EDITOR_CHECK(started == RV_OK);
    if (started != RV_OK) {
        return;
    }
    const auto pid = f.app.build.pid();
    const auto has_target = [candidate](const std::string &status) {
        return candidate ? status.contains("candidate") && status.contains("#41") : status.contains("build #1");
    };
    RV_EDITOR_CHECK(f.at_step("sources"));
    const std::string before_switch = f.status();
    RV_EDITOR_CHECK(has_target(before_switch));
    RV_EDITOR_CHECK(before_switch.contains("sources"));

    rv_editor_shell_switch(f.shell, destination);
    RV_EDITOR_CHECK(f.shell.active == destination);
    f.allow("compile");
    RV_EDITOR_CHECK(f.at_step("compile"));
    RV_EDITOR_CHECK(f.app.build.pid() == pid);
    const std::string after_switch = f.status();
    RV_EDITOR_CHECK(has_target(after_switch));
    RV_EDITOR_CHECK(after_switch.contains("compile"));

    if (outcome == rv_editor_build_state::cancelled) {
        f.cancel_from_controls();
    } else {
        f.allow("finish");
    }
    RV_EDITOR_CHECK(f.await([&] {
        return !f.app.build.busy() && !f.app.release.building;
    }));
    RV_EDITOR_CHECK(f.app.build.state() == outcome);

    const bool succeeded = outcome == rv_editor_build_state::succeeded;
    if (candidate) {
        RV_EDITOR_CHECK(fs::exists(previous_image));
        RV_EDITOR_CHECK(fs::exists(target_image) == succeeded);
        RV_EDITOR_CHECK(f.app.release.candidates.size() == (succeeded ? 2u : 1u));
    } else {
        RV_EDITOR_CHECK(f.app.build.last_success().has_value() == succeeded);
        RV_EDITOR_CHECK(f.app.release.candidates.empty());
    }
    const std::string ended = f.status();
    RV_EDITOR_CHECK(has_target(ended));
    RV_EDITOR_CHECK(ended.contains(rv_editor_build_state_name(outcome)));
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_project_build_finishes_after_switch_to_burn)
{
    check_build_after_switch(false, rv_editor_build_state::succeeded);
}

RV_EDITOR_TEST(rv_editor_test_project_build_cancels_from_burn)
{
    check_build_after_switch(false, rv_editor_build_state::cancelled);
}

RV_EDITOR_TEST(rv_editor_test_candidate_build_finishes_after_switch_to_code)
{
    check_build_after_switch(true, rv_editor_build_state::succeeded);
}

RV_EDITOR_TEST(rv_editor_test_candidate_build_cancels_from_code)
{
    check_build_after_switch(true, rv_editor_build_state::cancelled);
}

RV_EDITOR_TEST(rv_editor_test_candidate_spawn_failure_keeps_completed_project_build)
{
    build_layout f("code");
    f.allow("compile");
    f.allow("finish");
    const int started = rv_editor_app_build(f.app);
    RV_EDITOR_CHECK(started == RV_OK);
    if (started != RV_OK) {
        return;
    }
    const auto pid = f.app.build.pid();
    RV_EDITOR_CHECK(pid > 0);
    RV_EDITOR_CHECK(f.await([&] {
        return !f.app.build.busy();
    }));
    RV_EDITOR_CHECK(f.app.build.state() == rv_editor_build_state::succeeded);
    RV_EDITOR_CHECK(f.app.build.exit_status().exited);
    RV_EDITOR_CHECK(f.app.build.exit_status().code == 0);
    const auto artifact = f.app.build.last_success();
    RV_EDITOR_CHECK(artifact.has_value());
    if (!artifact) {
        return;
    }

    rv_editor_shell_switch(f.shell, rv_editor_layout_preset::burn);
    f.app.tools.burner.path = f.where / "missing-burner";
    RV_EDITOR_CHECK(rv_editor_app_build_candidate(f.app) == RV_ERR_NOENT);
    RV_EDITOR_CHECK(f.app.build.state() == rv_editor_build_state::succeeded);
    RV_EDITOR_CHECK(f.app.build.image().empty());
    RV_EDITOR_CHECK(f.app.build.target_number() == artifact->number);
    const auto &last_success = f.app.build.last_success();
    RV_EDITOR_CHECK(last_success && last_success->number == artifact->number && last_success->dir == artifact->dir);
    RV_EDITOR_CHECK(fs::is_directory(artifact->dir));
    RV_EDITOR_CHECK(f.app.build.pid() == pid);
    RV_EDITOR_CHECK(f.app.build.exit_status().exited);
    RV_EDITOR_CHECK(f.app.build.exit_status().code == 0);
}
