// F1 and AC7: Create, Open, Output and Review Changes across project transitions.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_panes.hpp"
#include "pdk/rv_err.h"
#include "project/rv_editor_templates.hpp"
#include "rv_editor_test_export_tile.hpp"
#include "rv_editor_test_project_directory.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

namespace fs = std::filesystem;
constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr size_t template_index = 1;

template <class Draw>
bool click(const Draw &draw, std::string_view label)
{
    draw();
    const auto record = draw();
    const auto button = std::ranges::find_if(record.entries, [label](const auto &entry) {
        return std::string_view(entry.label).substr(0, entry.label.find("##")) == label && entry.x1 > entry.x0 &&
            entry.y1 > entry.y0;
    });
    if (button == record.entries.end()) {
        return false;
    }
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent((button->x0 + button->x1) / 2, (button->y0 + button->y1) / 2);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    return true;
}

struct fixture {
    project_directory dir;
    rv_editor_test_imgui gui{ true };
    rv_editor_shell shell;
    fs::path first;

    bool open_first()
    {
        RV_EDITOR_CHECK(!dir.root.empty());
        if (dir.root.empty()) {
            return false;
        }
        first = dir.project("project-a");
        open(first);
        const bool opened = shell.app.project.open && shell.app.project.root == first;
        RV_EDITOR_CHECK(opened);
        return opened;
    }

    void open(const fs::path &path)
    {
        rv_editor_shell_request_open(shell, path);
        rv_editor_shell_after_save(shell);
    }

    template <class Page>
    rv_editor_probe_record draw(const Page &page)
    {
        return rv_editor_probe_draw(
            gui,
            1100,
            [&](auto &) {
                page();
            },
            500);
    }
};

// The test executable doesn't always ship templates. Supply just a valid manifest
// in the resource location if the regular template is absent.
struct scoped_template {
    fs::path path;
    bool created = false;
    bool created_parent = false;

    bool prepare()
    {
        const auto templates = rv_editor_templates();
        if (templates.size() <= template_index || templates[template_index].dir.empty()) {
            return false;
        }
        const auto &entry = templates[template_index];
        path = entry.dir;
        if (!fs::is_directory(path)) {
            created_parent = fs::create_directories(path.parent_path());
            created = fs::create_directory(path);
            std::ofstream(path / "disc.toml")
                << "[disc]\nid = \"" << entry.id << "\"\ntitle = \"Template\"\n[build]\nsources = [\"src/*.cpp\"]\n";
        }
        return fs::is_regular_file(path / "disc.toml");
    }

    ~scoped_template()
    {
        std::error_code error;
        if (created) {
            fs::remove_all(path, error);
        }
        if (created_parent) {
            fs::remove(path.parent_path(), error);
        }
    }
};

enum class project_blocker {
    build,
    export_disc,
    close_review
};

bool start_close_review(fixture &f)
{
    const auto scene = f.first / "unsaved.scene.toml";
    std::string error;
    const int saved = rv_editor_scene_save(rv_editor_scene_make(scene), error);
    RV_EDITOR_CHECK(saved == RV_OK);
    const int opened = rv_editor_app_scene_open(f.shell.app, scene);
    RV_EDITOR_CHECK(opened == RV_OK && f.shell.app.scene != nullptr);
    if (opened != RV_OK || f.shell.app.scene == nullptr) {
        return false;
    }
    rv_editor_scene_add(*f.shell.app.scene, "group", {});
    rv_editor_shell_request_close_project(f.shell);
    const bool reviewing_close = f.shell.leaving == rv_editor_shell::rv_editor_leave::close_project;
    RV_EDITOR_CHECK(reviewing_close);
    return reviewing_close;
}

bool start_blocker(fixture &f, project_blocker blocker)
{
    if (blocker == project_blocker::close_review) {
        return start_close_review(f);
    }
    auto &app = f.shell.app;
    app.tools.burner.path = export_tile::script(f.dir.root, "exec sleep 30");
    app.tools.baker.path = app.tools.burner.path;
    std::string error;
    const int guard = rv_editor_guard_start(error);
    RV_EDITOR_CHECK(guard == RV_OK || guard == RV_ERR_BUSY);
    int started = RV_OK;
    if (blocker == project_blocker::build) {
        started = app.build.start(app.project, app.tools, app.log, error);
    } else {
        rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
        started = rv_editor_pane_export_start(app);
    }
    RV_EDITOR_CHECK(started == RV_OK);
    RV_EDITOR_CHECK(rv_editor_shell_why_not_project_switch(f.shell) != nullptr);
    return started == RV_OK && rv_editor_shell_why_not_project_switch(f.shell) != nullptr;
}

template <class Job>
void cancel_job(Job &operation, rv_editor_log &log)
{
    std::string error;
    RV_EDITOR_CHECK(operation.cancel(error) == RV_OK);
    for (int i = 0; i < 300 && operation.busy(); ++i) {
        RV_EDITOR_CHECK(operation.update(log, error) == RV_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    RV_EDITOR_CHECK(operation.state() == rv_editor_build_state::cancelled);
}

void finish_blocker(fixture &f, project_blocker blocker)
{
    auto &app = f.shell.app;
    if (blocker == project_blocker::close_review) {
        RV_EDITOR_CHECK(f.shell.leaving == rv_editor_shell::rv_editor_leave::close_project);
        RV_EDITOR_CHECK(rv_editor_app_scene_dirty(app));
        rv_editor_shell_cancel_review(f.shell);
        std::string error;
        RV_EDITOR_CHECK(rv_editor_app_scene_save(app, error) == RV_OK);
        return;
    }
    if (blocker == project_blocker::build) {
        cancel_job(app.build, app.log);
    } else {
        cancel_job(app.exporter, app.log);
    }
    rv_editor_shell_after_save(f.shell);
    RV_EDITOR_CHECK(rv_editor_shell_why_not_project_switch(f.shell) == nullptr);
    RV_EDITOR_CHECK(app.project.open && app.project.root == f.first);
}

void prepare_draft(fixture &f, const rv_editor_new_project &draft)
{
    auto &start = f.shell.start;
    std::snprintf(start.name, sizeof(start.name), "%s", draft.name.c_str());
    std::snprintf(start.id, sizeof(start.id), "%s", draft.disc_id.c_str());
    std::snprintf(start.dir, sizeof(start.dir), "%s", draft.parent.c_str());
    start.id_edited = true;
    start.template_index = draft.template_index;
    rv_editor_shell_page(f.shell, rv_editor_start_page::new_project);
}

void check_no_created_project(const rv_editor_new_project &draft, const fs::path &created)
{
    RV_EDITOR_CHECK(!fs::exists(created));
    RV_EDITOR_CHECK(!fs::exists(draft.parent / ("." + draft.disc_id + ".creating")));
}

void check_blocked_create(project_blocker blocker)
{
    fixture f;
    scoped_template template_resource;
    const bool available = template_resource.prepare();
    RV_EDITOR_CHECK(available);
    if (!available || !f.open_first()) {
        return;
    }

    const rv_editor_new_project draft{ "New Game", "new_game", f.dir.root, template_index };
    const fs::path created = draft.parent / draft.disc_id;
    RV_EDITOR_CHECK(rv_editor_new_project_problem(draft, rv_editor_templates()).empty());
    prepare_draft(f, draft);
    if (!start_blocker(f, blocker)) {
        return;
    }
    const auto draw = [&] {
        return f.draw([&] {
            rv_editor_page_new_project(f.shell, theme);
        });
    };
    const char *create = rv_editor_text("shell_start_new.create_project");
    RV_EDITOR_CHECK(click(draw, create));
    rv_editor_shell_after_save(f.shell);

    const auto &start = f.shell.start;
    const bool retained = start.name == draft.name && start.id == draft.disc_id && fs::path(start.dir) == draft.parent &&
        start.template_index == draft.template_index && start.id_edited;
    RV_EDITOR_CHECK(retained);
    check_no_created_project(draft, created);
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);

    finish_blocker(f, blocker);
    if (retained && !fs::exists(created)) {
        RV_EDITOR_CHECK(click(draw, create));
        rv_editor_shell_after_save(f.shell);
        RV_EDITOR_CHECK(fs::is_regular_file(created / "disc.toml"));
        RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == created);
    }
}

void check_blocked_open(project_blocker blocker)
{
    fixture f;
    if (!f.open_first()) {
        return;
    }
    const fs::path target = f.dir.project("project-b");
    const char *open = rv_editor_text("shell_start.open_project");
    rv_editor_shell_open_project(f.shell);
    const auto draw = [&] {
        return f.draw([&] {
            rv_editor_page_open_project(f.shell, theme);
        });
    };
    draw();
    auto &browser = f.shell.open_browser;
    browser.selected = target;
    RV_EDITOR_CHECK(browser.dir == f.dir.root);
    if (!start_blocker(f, blocker)) {
        return;
    }
    RV_EDITOR_CHECK(click(draw, open));
    rv_editor_shell_after_save(f.shell);

    const bool retained = browser.dir == f.dir.root && browser.selected == target;
    RV_EDITOR_CHECK(retained);
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);

    finish_blocker(f, blocker);
    if (retained) {
        RV_EDITOR_CHECK(click(draw, open));
        rv_editor_shell_after_save(f.shell);
        RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == target);
    }
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_project_ui_create_during_build_keeps_the_draft_and_writes_nothing)
{
    check_blocked_create(project_blocker::build);
}

RV_EDITOR_TEST(rv_editor_test_project_ui_create_during_export_keeps_the_draft_and_writes_nothing)
{
    check_blocked_create(project_blocker::export_disc);
}

RV_EDITOR_TEST(rv_editor_test_project_ui_open_during_build_keeps_the_browser_selection)
{
    check_blocked_open(project_blocker::build);
}

RV_EDITOR_TEST(rv_editor_test_project_ui_open_during_export_keeps_the_browser_selection)
{
    check_blocked_open(project_blocker::export_disc);
}

RV_EDITOR_TEST(rv_editor_test_project_ui_create_during_close_review_keeps_the_draft_and_writes_nothing)
{
    check_blocked_create(project_blocker::close_review);
}

RV_EDITOR_TEST(rv_editor_test_project_ui_open_during_close_review_keeps_the_browser_selection)
{
    check_blocked_open(project_blocker::close_review);
}

RV_EDITOR_TEST(rv_editor_test_project_ui_output_shows_new_project_lines_after_switch)
{
    fixture f;
    if (!f.open_first()) {
        return;
    }
    auto &app = f.shell.app;
    constexpr rv_editor_pane_id pane = 1;
    const char *copy = rv_editor_text("pane_output_controls.button_copy");
    auto &output = app.outputs[pane];
    output.run_pid = 101;
    output.run_label = "Project A run";
    app.log
        .add(rv_editor_log_source::build, rv_editor_log_level::info, "line from Project A", rv_editor_log_channel::out, 101, 1);
    const auto draw = [&] {
        return f.draw([&] {
            rv_editor_pane_output(app, pane, theme);
        });
    };
    RV_EDITOR_CHECK(click(draw, copy));
    RV_EDITOR_CHECK(std::string_view(ImGui::GetClipboardText()).find("line from Project A") != std::string_view::npos);

    const fs::path target = f.dir.project("project-b");
    f.open(target);
    RV_EDITOR_CHECK(app.project.open && app.project.root == target);
    app.log
        .add(rv_editor_log_source::build, rv_editor_log_level::info, "line from Project B", rv_editor_log_channel::out, 202, 1);
    RV_EDITOR_CHECK(click(draw, copy));
    RV_EDITOR_CHECK(std::string_view(ImGui::GetClipboardText()).find("line from Project B") != std::string_view::npos);
    RV_EDITOR_CHECK(rv_editor_output_title(app, pane).find("Project A run") == std::string::npos);
}

RV_EDITOR_TEST(rv_editor_test_project_ui_close_review_message_fits_localized_minimum)
{
    fixture f;
    if (!f.open_first() || !start_close_review(f)) {
        return;
    }
    std::string error;
    const auto override = f.dir.root / "texts.toml";
    const std::string message =
        "Localized close instructions: save the changed files before closing the current project, or discard the changes "
        "if you do not need them. Choose Return to keep the project open and continue editing all of these files.";
    std::ofstream(override) << "[shell_save]\nproject_closes = \"" << message << "\"\n";
    RV_EDITOR_CHECK(rv_editor_text_load(override, error) == RV_OK);
    RV_EDITOR_CHECK(rv_editor_text("shell_save.project_closes") == message);

    const auto minimum = stable_tiles::pane_minimum(f.gui, rv_editor_pane_kind::review_changes);
    float content_bottom = 0.0f;
    f.gui.frame(
        [&] {
            rv_editor_page_review(f.shell, theme);
            content_bottom = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y;
        },
        minimum.w,
        minimum.h);
    if (content_bottom > minimum.h) {
        std::fprintf(stderr, "  Close message reaches y=%.1f with a minimum height of %.1f\n", content_bottom, minimum.h);
    }
    RV_EDITOR_CHECK(content_bottom <= minimum.h);
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == RV_OK);
}
