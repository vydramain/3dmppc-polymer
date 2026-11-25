// AC7: project operations and their results belong to the project that started them.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "app/rv_editor_app_findings.hpp"
#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_panes.hpp"
#include "pdk/rv_err.h"
#include "project/rv_editor_templates.hpp"
#include "rv_editor_test_export_tile.hpp"
#include "rv_editor_test_pane_state.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

namespace fs = std::filesystem;

struct project_directory {
    fs::path root = export_tile::scratch("project_lifecycle");

    ~project_directory()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }

    fs::path project(const char *name) const
    {
        const fs::path path = root / name;
        fs::create_directories(path);
        std::ofstream(path / "disc.toml") << "[disc]\nid = \"" << name << "\"\ntitle = \"" << name << "\"\n";
        return path;
    }
};

template <class Draw>
bool click(const Draw &draw, const char *label)
{
    const auto drawn = draw();
    const auto *button = pane_state::find_item(drawn, label);
    if (button == nullptr || button->x1 <= button->x0 || button->y1 <= button->y0) {
        return false;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((button->x0 + button->x1) / 2, (button->y0 + button->y1) / 2);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    return true;
}

bool click_export_cancel(rv_editor_test_imgui &gui, rv_editor_app &app)
{
    return click(
               [&] {
                   return export_tile::draw_tile(gui, app, export_tile::wide);
               },
               rv_editor_text("pane_export.cancel_button")) &&
        app.exporter.state() == rv_editor_build_state::cancelling;
}

void check_export_blocks_project_leave(bool close)
{
    project_directory directory;
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    auto &app = shell.app;
    const fs::path origin = directory.project("origin");
    const fs::path target = directory.project("target");
    rv_editor_shell_request_open(shell, origin);
    rv_editor_shell_after_save(shell);
    const bool opened = app.project.open && app.project.root == origin;
    RV_EDITOR_CHECK(opened);
    if (!opened) {
        return;
    }

    const auto burner = export_tile::script(directory.root, "exec sleep 30");
    app.tools.burner.path = burner;
    app.tools.baker.path = burner;
    std::string error;
    const int guard = rv_editor_guard_start(error);
    RV_EDITOR_CHECK(guard == RV_OK || guard == RV_ERR_BUSY);
    rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
    const int started = rv_editor_pane_export_start(app);
    RV_EDITOR_CHECK(started == RV_OK);
    if (started != RV_OK) {
        return;
    }
    RV_EDITOR_CHECK(app.exporter.state() == rv_editor_build_state::building);

    const auto leave = [&] {
        if (close) {
            rv_editor_shell_request_close_project(shell);
        } else {
            rv_editor_shell_request_open(shell, target);
        }
        rv_editor_shell_after_save(shell);
    };

    const char *reason = rv_editor_shell_why_not_project_switch(shell);
    RV_EDITOR_CHECK(reason != nullptr && std::string_view(reason).find("export") != std::string_view::npos);
    leave();
    const bool origin_still_open = app.project.open && app.project.root == origin;
    RV_EDITOR_CHECK(origin_still_open);
    RV_EDITOR_CHECK(app.exporter.state() == rv_editor_build_state::building);

    const bool cancelled_in_tile = click_export_cancel(gui, app);
    RV_EDITOR_CHECK(!origin_still_open || cancelled_in_tile);
    if (!cancelled_in_tile && app.exporter.state() == rv_editor_build_state::building) {
        RV_EDITOR_CHECK(app.exporter.cancel(error) == RV_OK);
    }
    export_tile::settle(app, rv_editor_build_state::cancelled);
    RV_EDITOR_CHECK(app.exporter.state() == rv_editor_build_state::cancelled);
    if (!origin_still_open) {
        return;
    }

    rv_editor_shell_after_save(shell);
    RV_EDITOR_CHECK(app.project.open && app.project.root == origin);
    RV_EDITOR_CHECK(rv_editor_shell_why_not_project_switch(shell) == nullptr);
    leave();
    if (close) {
        RV_EDITOR_CHECK(!app.project.open);
        RV_EDITOR_CHECK(
            shell.start_page == rv_editor_start_page::recent || shell.start_page == rv_editor_start_page::open_project);
    } else {
        RV_EDITOR_CHECK(app.project.open && app.project.root == target);
    }
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_project_export_lifecycle_switch_waits_for_export)
{
    check_export_blocks_project_leave(false);
}

RV_EDITOR_TEST(rv_editor_test_project_export_lifecycle_close_waits_for_export)
{
    check_export_blocks_project_leave(true);
}

RV_EDITOR_TEST(rv_editor_test_project_export_lifecycle_reopening_project_drops_completed_export)
{
    project_directory directory;
    rv_editor_shell shell;
    auto &app = shell.app;
    const auto root = directory.project("exported");
    rv_editor_shell_request_open(shell, root);
    rv_editor_shell_after_save(shell);
    RV_EDITOR_CHECK(app.project.open && app.project.root == root);

    const auto burner = export_tile::script(directory.root, "mkdir -p \"$6\" && echo x > \"$6/new.txt\"");
    app.tools.burner.path = burner;
    app.tools.baker.path = burner;
    std::string error;
    const int guard = rv_editor_guard_start(error);
    RV_EDITOR_CHECK(guard == RV_OK || guard == RV_ERR_BUSY);
    rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
    const int started = rv_editor_pane_export_start(app);
    RV_EDITOR_CHECK(started == RV_OK);
    if (started != RV_OK) {
        return;
    }
    export_tile::settle(app, rv_editor_build_state::succeeded);
    RV_EDITOR_CHECK(app.exporter.state() == rv_editor_build_state::succeeded);
    const fs::path result = app.exporter.result();
    RV_EDITOR_CHECK(fs::exists(result));

    rv_editor_shell_request_close_project(shell);
    rv_editor_shell_after_save(shell);
    RV_EDITOR_CHECK(!app.project.open);
    rv_editor_shell_request_open(shell, root);
    rv_editor_shell_after_save(shell);
    RV_EDITOR_CHECK(app.project.open && app.project.root == root);
    RV_EDITOR_CHECK(app.exporter.result().empty());
    RV_EDITOR_CHECK(fs::exists(result));
}

RV_EDITOR_TEST(rv_editor_test_project_playback_lifecycle_recent_switch_drops_stopped_session)
{
    project_directory directory;
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    auto &app = shell.app;
    const fs::path origin = directory.project("played");
    const fs::path target = directory.project("unplayed");
    rv_editor_shell_request_open(shell, origin);
    rv_editor_shell_after_save(shell);
    const bool opened = app.project.open && app.project.root == origin;
    RV_EDITOR_CHECK(opened);
    if (!opened) {
        return;
    }

    const std::string idle_status = rv_editor_status_text_make(app).runtime;
    pane_state::fake_console_options options;
    options.build_number = 7;
    const bool started = pane_state::start_fake_session(app, origin, options);
    RV_EDITOR_CHECK(started);
    if (!started) {
        return;
    }
    const bool stopped = pane_state::end_fake_session(app);
    RV_EDITOR_CHECK(stopped);
    if (!stopped) {
        return;
    }
    RV_EDITOR_CHECK(rv_editor_status_text_make(app).runtime != idle_status);
    RV_EDITOR_CHECK(!rv_editor_session_notes(app).empty());
    RV_EDITOR_CHECK(!app.session.facts().disc.empty());

    const auto draw_case = [&] {
        return rv_editor_probe_draw(
            gui,
            720.0f,
            [&](auto &) {
                rv_editor_pane_test_case(app, export_tile::theme);
            },
            500.0f);
    };
    const char *passed = rv_editor_text("findings.passed");
    draw_case();
    RV_EDITOR_CHECK(click(draw_case, passed));
    rv_editor_app_update(app);
    RV_EDITOR_CHECK(!app.findings.saved.empty() && fs::is_regular_file(app.findings.saved.back()));

    rv_editor_recent_add(target);
    const auto draw_recent = [&] {
        return rv_editor_probe_draw(
            gui,
            720.0f,
            [&](auto &) {
                rv_editor_shell_recent_menu(shell);
            },
            500.0f);
    };
    draw_recent();
    RV_EDITOR_CHECK(click(draw_recent, target.filename().c_str()));
    rv_editor_shell_after_save(shell);
    const bool switched = app.project.open && app.project.root == target;
    RV_EDITOR_CHECK(switched);
    if (!switched) {
        return;
    }

    RV_EDITOR_CHECK(rv_editor_status_text_make(app).runtime == idle_status);
    RV_EDITOR_CHECK(rv_editor_session_notes(app).empty());
    RV_EDITOR_CHECK(app.session.facts().disc.empty());
    draw_case();
    RV_EDITOR_CHECK(click(draw_case, passed));
    rv_editor_app_update(app);
    RV_EDITOR_CHECK(app.findings.saved.empty());
    const auto findings = app.project.state_dir / dir_name_findings;
    RV_EDITOR_CHECK(!fs::exists(findings) || fs::is_empty(findings));
}
