// AC7: Close Project and Open Recent use the existing unsaved changes review.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include "app/rv_editor_shell.hpp"
#include "pdk/rv_err.h"
#include "platform/rv_editor_process.hpp"
#include "project/rv_editor_templates.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_document_menu.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_project_directory.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

namespace fs = std::filesystem;
constexpr rv_editor_theme theme = rv_editor_theme_olive;

template <class Draw>
bool click(rv_editor_test_imgui &gui, const Draw &draw, std::string_view label)
{
    constexpr float width = 720.0f;
    constexpr float height = 500.0f;
    const auto frame = [&] {
        return rv_editor_probe_draw(
            gui,
            width,
            [&](auto &) {
                draw();
            },
            height);
    };
    frame();
    const auto record = frame();
    for (const auto &entry : record.entries) {
        if (std::string_view(entry.label).substr(0, entry.label.find("##")) != label || entry.x1 <= entry.x0 ||
            entry.y1 <= entry.y0) {
            continue;
        }
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent((entry.x0 + entry.x1) / 2, (entry.y0 + entry.y1) / 2);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        return true;
    }
    return false;
}

struct project_fixture {
    project_directory directory;
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    fs::path first;
    fs::path scene_path;
    std::string new_object;

    bool open()
    {
        RV_EDITOR_CHECK(!directory.root.empty());
        if (directory.root.empty()) {
            return false;
        }
        first = directory.project("first");
        rv_editor_shell_request_open(shell, first);
        rv_editor_shell_after_save(shell);
        const bool opened = shell.app.project.open && shell.app.project.root == first;
        RV_EDITOR_CHECK(opened);
        return opened;
    }

    bool edit_scene()
    {
        scene_path = first / "scenes" / "level.scene.toml";
        fs::create_directories(scene_path.parent_path());
        std::string error;
        const int saved = rv_editor_scene_save(rv_editor_scene_make(scene_path), error);
        RV_EDITOR_CHECK(saved == RV_OK);
        const int opened = rv_editor_app_scene_open(shell.app, scene_path);
        RV_EDITOR_CHECK(opened == RV_OK && shell.app.scene != nullptr);
        if (saved != RV_OK || opened != RV_OK || shell.app.scene == nullptr) {
            return false;
        }
        new_object = rv_editor_scene_add(*shell.app.scene, "group", {});
        RV_EDITOR_CHECK(rv_editor_app_scene_dirty(shell.app));
        return true;
    }

    bool review(const char *action)
    {
        rv_editor_shell_review_tab(shell);
        const bool pressed = click(
            gui,
            [&] {
                rv_editor_page_review(shell, theme);
            },
            rv_editor_text(action));
        rv_editor_shell_after_save(shell);
        return pressed;
    }

    bool recent(const fs::path &path)
    {
        const bool selected = click(
            gui,
            [&] {
                rv_editor_shell_recent_menu(shell);
            },
            path.filename().string());
        rv_editor_shell_after_save(shell);
        return selected;
    }

    void check_closed() const
    {
        RV_EDITOR_CHECK(!shell.app.project.open);
        RV_EDITOR_CHECK(shell.app.scene == nullptr);
        RV_EDITOR_CHECK(!shell.quit_now);
        RV_EDITOR_CHECK(
            shell.start_page == rv_editor_start_page::recent || shell.start_page == rv_editor_start_page::open_project);
    }
};

bool start_build(project_fixture &f, const char *commands)
{
    const fs::path burner = f.directory.root / "burner.sh";
    std::ofstream(burner) << "#!/bin/sh\n" << commands << '\n';
    fs::permissions(burner, fs::perms::owner_exec, fs::perm_options::add);
    rv_editor_app &app = f.shell.app;
    app.tools.burner.path = burner;
    app.tools.baker.path = burner;
    std::string error;
    const int guard = rv_editor_guard_start(error);
    RV_EDITOR_CHECK(guard == RV_OK || guard == RV_ERR_BUSY);
    const int started = app.build.start(app.project, app.tools, app.log, error);
    RV_EDITOR_CHECK(started == RV_OK);
    return started == RV_OK;
}

void settle_build(rv_editor_app &app)
{
    std::string error;
    for (int i = 0; i < 300 && app.build.busy(); ++i) {
        RV_EDITOR_CHECK(app.build.update(app.log, error) == RV_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_clean_close_returns_to_project_picker)
{
    project_fixture f;
    if (!f.open()) {
        return;
    }
    f.shell.start_page = rv_editor_start_page::manual;
    rv_editor_shell_request_close_project(f.shell);
    rv_editor_shell_after_save(f.shell);
    f.check_closed();
    RV_EDITOR_CHECK(f.shell.leaving == rv_editor_shell::rv_editor_leave::none);
    RV_EDITOR_CHECK(fs::is_regular_file(f.first / "disc.toml"));
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_return_from_close_keeps_unsaved_scene)
{
    project_fixture f;
    if (!f.open() || !f.edit_scene()) {
        return;
    }
    rv_editor_shell_request_close_project(f.shell);
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);
    const bool returned = f.review("shell_save.return_button");
    RV_EDITOR_CHECK(returned);
    if (!returned) {
        return;
    }
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);
    RV_EDITOR_CHECK(rv_editor_app_scene_dirty(f.shell.app));
    RV_EDITOR_CHECK(f.shell.app.scene != nullptr && rv_editor_scene_find(f.shell.app.scene->scene, f.new_object) >= 0);
    RV_EDITOR_CHECK(f.shell.leaving == rv_editor_shell::rv_editor_leave::none);
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_save_all_then_close_writes_scene)
{
    project_fixture f;
    if (!f.open() || !f.edit_scene()) {
        return;
    }
    rv_editor_shell_request_close_project(f.shell);
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);
    const bool saved = f.review("shell_save.save_all_button");
    RV_EDITOR_CHECK(saved);
    if (!saved) {
        return;
    }
    f.check_closed();
    rv_editor_scene disk;
    std::string error;
    RV_EDITOR_CHECK(rv_editor_scene_load(f.scene_path, disk, error) == RV_OK);
    RV_EDITOR_CHECK(rv_editor_scene_find(disk, f.new_object) >= 0);
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_discard_then_close_preserves_saved_scene)
{
    project_fixture f;
    if (!f.open() || !f.edit_scene()) {
        return;
    }
    rv_editor_shell_request_close_project(f.shell);
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);
    const bool discarded = f.review("shell_save.discard_all_button");
    RV_EDITOR_CHECK(discarded);
    if (!discarded) {
        return;
    }
    f.check_closed();
    rv_editor_scene disk;
    std::string error;
    RV_EDITOR_CHECK(rv_editor_scene_load(f.scene_path, disk, error) == RV_OK);
    RV_EDITOR_CHECK(rv_editor_scene_find(disk, f.new_object) < 0);
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_close_keeps_unsaved_scene_when_save_fails)
{
    project_fixture f;
    if (!f.open() || !f.edit_scene()) {
        return;
    }
    fs::rename(f.scene_path, f.scene_path.string() + ".saved");
    RV_EDITOR_CHECK(fs::create_directory(f.scene_path));
    rv_editor_shell_request_close_project(f.shell);
    const bool saved = f.review("shell_save.save_all_button");
    RV_EDITOR_CHECK(saved);
    if (!saved) {
        return;
    }
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);
    RV_EDITOR_CHECK(rv_editor_app_scene_dirty(f.shell.app));
    RV_EDITOR_CHECK(!f.shell.save_failed.empty());
    RV_EDITOR_CHECK(f.shell.app.scene != nullptr && rv_editor_scene_find(f.shell.app.scene->scene, f.new_object) >= 0);
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_recent_menu_opens_selected_entry_from_start)
{
    project_fixture f;
    RV_EDITOR_CHECK(!f.directory.root.empty());
    if (f.directory.root.empty()) {
        return;
    }
    const fs::path selected = f.directory.project("selected-recent");
    const fs::path other = f.directory.project("other-recent");
    rv_editor_recent_add(selected);
    rv_editor_recent_add(other);
    f.shell.start_selected = selected;
    const bool opened_from_start_page = click(
        f.gui,
        [&] {
            rv_editor_page_recent(f.shell, theme);
        },
        rv_editor_text("shell_start.open_project"));
    rv_editor_shell_after_save(f.shell);
    RV_EDITOR_CHECK(opened_from_start_page);
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == selected);

    rv_editor_recent_add(other); // Reopening a project moved it to the front. Select a different entry again.
    rv_editor_shell on_start;
    const bool selected_from_menu = click(
        f.gui,
        [&] {
            rv_editor_shell_recent_menu(on_start);
        },
        selected.filename().string());
    rv_editor_shell_after_save(on_start);
    RV_EDITOR_CHECK(selected_from_menu);
    RV_EDITOR_CHECK(on_start.app.project.open && on_start.app.project.root == selected);
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_recent_menu_switches_clean_project)
{
    project_fixture f;
    if (!f.open()) {
        return;
    }
    const fs::path second = f.directory.project("second-recent");
    rv_editor_recent_add(second);
    RV_EDITOR_CHECK(f.recent(second));
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == second);
    RV_EDITOR_CHECK(f.shell.leaving == rv_editor_shell::rv_editor_leave::none);
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_recent_menu_respects_unsaved_changes)
{
    project_fixture f;
    if (!f.open() || !f.edit_scene()) {
        return;
    }
    const fs::path second = f.directory.project("second-recent");
    rv_editor_recent_add(second);
    const bool chose_recent = f.recent(second);
    RV_EDITOR_CHECK(chose_recent);
    if (!chose_recent) {
        return;
    }
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);
    const bool returned = f.review("shell_save.return_button");
    RV_EDITOR_CHECK(returned);
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);
    RV_EDITOR_CHECK(rv_editor_app_scene_dirty(f.shell.app));
    RV_EDITOR_CHECK(f.shell.app.scene != nullptr && rv_editor_scene_find(f.shell.app.scene->scene, f.new_object) >= 0);
    RV_EDITOR_CHECK(f.recent(second));
    const bool discarded = f.review("shell_save.discard_all_button");
    RV_EDITOR_CHECK(discarded);
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == second);
    rv_editor_scene disk;
    std::string error;
    RV_EDITOR_CHECK(rv_editor_scene_load(f.scene_path, disk, error) == RV_OK);
    RV_EDITOR_CHECK(rv_editor_scene_find(disk, f.new_object) < 0);
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_recent_switch_cannot_run_previous_project_build)
{
    project_fixture f;
    if (!f.open() || !start_build(f, "mkdir -p \"$4\"")) {
        return;
    }
    rv_editor_app &app = f.shell.app;
    settle_build(app);
    RV_EDITOR_CHECK(app.build.state() == rv_editor_build_state::succeeded);
    RV_EDITOR_CHECK(rv_editor_app_why_not_run_last(app) == nullptr);
    const auto add_profile = rv_editor_pane_request_kind::profile_add;
    RV_EDITOR_CHECK(rv_editor_app_request(app, { .kind = add_profile }) == RV_OK);
    RV_EDITOR_CHECK(rv_editor_app_request_pending(app, add_profile));

    const fs::path second = f.directory.project("second-recent");
    const auto profiles = rv_editor_run_config_load(second).profiles.size();
    rv_editor_recent_add(second);
    RV_EDITOR_CHECK(f.recent(second));
    RV_EDITOR_CHECK(app.project.open && app.project.root == second);
    const char *why = rv_editor_app_why_not_run_last(app);
    RV_EDITOR_CHECK(why != nullptr && std::string_view(why) == rv_editor_text("app_why_not.no_build_succeeded"));
    RV_EDITOR_CHECK(!rv_editor_app_request_pending(app, add_profile));
    rv_editor_app_update(app);
    RV_EDITOR_CHECK(rv_editor_run_config_load(second).profiles.size() == profiles);
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_recent_switch_during_build_keeps_project)
{
    project_fixture f;
    if (!f.open() || !start_build(f, "exec sleep 30")) {
        return;
    }
    const fs::path second = f.directory.project("second-recent");
    rv_editor_recent_add(second);
    RV_EDITOR_CHECK(f.recent(second));
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);

    rv_editor_app &app = f.shell.app;
    RV_EDITOR_CHECK(app.build.state() == rv_editor_build_state::building);
    std::string error;
    if (app.build.state() == rv_editor_build_state::building) {
        RV_EDITOR_CHECK(app.build.cancel(error) == RV_OK);
    }
    settle_build(app);
    RV_EDITOR_CHECK(!app.build.busy());
    rv_editor_shell_after_save(f.shell);
    RV_EDITOR_CHECK(app.project.root == f.first);
    RV_EDITOR_CHECK(f.recent(second));
    RV_EDITOR_CHECK(app.project.open && app.project.root == second);
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_close_during_build_keeps_project)
{
    project_fixture f;
    if (!f.open() || !start_build(f, "exec sleep 30")) {
        return;
    }
    rv_editor_shell_request_close_project(f.shell);
    rv_editor_shell_after_save(f.shell);
    RV_EDITOR_CHECK(f.shell.app.project.open && f.shell.app.project.root == f.first);

    rv_editor_app &app = f.shell.app;
    RV_EDITOR_CHECK(app.build.state() == rv_editor_build_state::building);
    std::string error;
    if (app.build.state() == rv_editor_build_state::building) {
        RV_EDITOR_CHECK(app.build.cancel(error) == RV_OK);
    }
    settle_build(app);
    RV_EDITOR_CHECK(!app.build.busy());
    rv_editor_shell_after_save(f.shell);
    RV_EDITOR_CHECK(app.project.root == f.first);
    rv_editor_shell_request_close_project(f.shell);
    rv_editor_shell_after_save(f.shell);
    f.check_closed();
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_close_reviews_latest_code_edit)
{
    menu_fixture f;
    auto &shell = f.shell;
    auto &nvim = shell.app.nvim;
    const auto file = f.root / "latest.txt";
    std::ofstream(file) << "on disk\n";
    if (!f.start_code() || !f.open_code(f.first, file)) {
        return;
    }

    // Make the edit in Neovim without updating the editor's cached modified flags.
    const auto win = nvim.window_for(f.first);
    nvim.input("<Cmd>lua local b = vim.api.nvim_win_get_buf(" + std::to_string(win) +
        "); vim.opt.eventignore:append('BufModifiedSet'); vim.api.nvim_buf_set_lines(b, 0, 1, false, {'latest edit'}); "
        "vim.opt.eventignore:remove('BufModifiedSet'); vim.fn.writefile(vim.api.nvim_buf_get_lines(b, 0, 1, true), "
        "'edit-ready')<CR>");
    const bool edited = f.wait_for([&] {
        return read_file(f.root / "edit-ready") == "latest edit\n";
    });
    RV_EDITOR_CHECK(edited && nvim.modified().empty());
    if (!edited || !nvim.modified().empty()) {
        return;
    }

    rv_editor_shell_request_close_project(shell);
    std::string review;
    const bool reviewed = f.wait_for([&] {
        rv_editor_shell_update(shell, false);
        f.draw(&review);
        rv_editor_shell_after_save(shell);
        return !shell.app.project.open || includes(review, "latest.txt");
    });
    RV_EDITOR_CHECK(reviewed && shell.app.project.open && includes(review, "latest.txt"));
    RV_EDITOR_CHECK(read_file(file) == "on disk\n");
    if (!reviewed || !shell.app.project.open || !includes(review, "latest.txt")) {
        return;
    }

    const auto discard = f.find(rv_editor_text("shell_save.discard_all_button"));
    RV_EDITOR_CHECK(discard.has_value());
    if (!discard) {
        return;
    }
    f.click(*discard);
    RV_EDITOR_CHECK(f.wait_for([&] {
        rv_editor_shell_update(shell, false);
        f.draw();
        rv_editor_shell_after_save(shell);
        return !shell.app.project.open;
    }));
    RV_EDITOR_CHECK(read_file(file) == "on disk\n");
}
