#include <cstdlib>

#include "rv_editor_test_document_menu.hpp"

namespace
{

using leave = rv_editor_shell::rv_editor_leave;

struct scoped_recent_config {
    std::optional<std::string> previous;

    explicit scoped_recent_config(const fs::path &root)
    {
        if (const char *config = std::getenv("XDG_CONFIG_HOME")) {
            previous = config;
        }
        RV_EDITOR_CHECK(::setenv("XDG_CONFIG_HOME", root.c_str(), 1) == 0);
    }

    ~scoped_recent_config()
    {
        if (previous) {
            ::setenv("XDG_CONFIG_HOME", previous->c_str(), 1);
        } else {
            ::unsetenv("XDG_CONFIG_HOME");
        }
    }
};

bool open_text(menu_fixture &fixture, const fs::path &path)
{
    std::ofstream(path) << "one\n";
    return fixture.start_code() && fixture.open_code(fixture.first, path);
}

fs::path make_project(const fs::path &root)
{
    fs::create_directory(root);
    std::ofstream(root / "disc.toml") << "[disc]\nid = 'other-project'\ntitle = 'Other Project'\n";
    return root;
}

bool click_review(menu_fixture &fixture, const char *label)
{
    const auto button = fixture.find(label);
    RV_EDITOR_CHECK(button.has_value());
    if (button) {
        fixture.click(*button);
    }
    return button.has_value();
}

bool wait_for_review(menu_fixture &fixture, leave action, std::string_view file)
{
    const bool ready = fixture.wait_for([&] {
        rv_editor_shell_update(fixture.shell);
        std::string review;
        fixture.draw(&review);
        return fixture.shell.quit_now || (fixture.shell.leaving == action && includes(review, file));
    });
    RV_EDITOR_CHECK(!fixture.shell.quit_now);
    if (fixture.shell.quit_now) {
        return false;
    }
    RV_EDITOR_CHECK(ready);
    return ready;
}

// Edit real text while delaying the push notification for that change. The
// marker proves the edit happened in Neovim before the shell makes its decision.
bool edit_without_notification(menu_fixture &fixture, std::string_view prefix)
{
    const auto marker = fixture.root / "edit-ready";
    fs::remove(marker);
    auto &nvim = fixture.shell.app.nvim;
    const auto win = nvim.window_for(fixture.first);
    nvim.input("<Cmd>lua local b = vim.api.nvim_win_get_buf(" + std::to_string(win) +
        "); vim.opt.eventignore:append('BufModifiedSet'); vim.api.nvim_buf_set_text(b, 0, 0, 0, 0, {'" + std::string(prefix) +
        "'}); vim.opt.eventignore:remove('BufModifiedSet'); "
        "vim.fn.writefile(vim.api.nvim_buf_get_lines(b, 0, 1, true), 'edit-ready')<CR>");
    const bool edited = fixture.wait_for([&] {
        return fs::exists(marker) && includes(read_file(marker), prefix);
    });
    RV_EDITOR_CHECK(edited);
    return edited;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_project_open_checks_latest_code_changes_before_leaving)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto text = fixture.root / "first.txt";
    const scoped_recent_config config(fixture.root / "config");
    const auto target = make_project(fixture.root / "other");
    if (!open_text(fixture, text) || !edit_without_notification(fixture, "latest-")) {
        return;
    }
    RV_EDITOR_CHECK(shell.app.nvim.modified().empty());

    rv_editor_shell_request_open(shell, target);
    RV_EDITOR_CHECK(shell.app.project.root == fixture.root);
    if (shell.app.project.root != fixture.root || !wait_for_review(fixture, leave::open, "first.txt")) {
        return;
    }
    if (!click_review(fixture, "Return")) {
        return;
    }
    rv_editor_shell_update(shell);
    RV_EDITOR_CHECK(shell.app.project.root == fixture.root && shell.leaving == leave::none);
    RV_EDITOR_CHECK(shell.app.nvim.modified().size() == 1);
    RV_EDITOR_CHECK(read_file(text) == "one\n");

    rv_editor_shell_request_open(shell, target);
    if (wait_for_review(fixture, leave::open, "first.txt") && click_review(fixture, "Discard All")) {
        RV_EDITOR_CHECK(fixture.wait_for([&] {
            rv_editor_shell_update(shell);
            return shell.app.project.root == target;
        }));
        RV_EDITOR_CHECK(read_file(text) == "one\n");
    }
}

RV_EDITOR_TEST(rv_editor_test_project_open_discard_keeps_documents_when_destination_disappears)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    auto &nvim = shell.app.nvim;
    const scoped_recent_config config(fixture.root / "config");
    const auto text = fixture.root / "first.txt";
    const auto other_text = fixture.root / "second.txt";
    const auto target = make_project(fixture.root / "other");
    std::ofstream(other_text) << "two\n";
    if (!open_text(fixture, text) || !fixture.open_code(fixture.second, other_text) ||
        !fixture.modify_code(fixture.first, "unsaved-")) {
        return;
    }
    fixture.edit_scene();
    const auto first_win = nvim.window_for(fixture.first);
    const auto second_win = nvim.window_for(fixture.second);
    rv_editor_shell_request_open(shell, target);
    if (!wait_for_review(fixture, leave::open, "first.txt")) {
        return;
    }

    // Hold Neovim while Open submits its work. The target exists while the
    // shell validates it and is gone by the time Neovim can switch projects.
    std::ofstream(fixture.root / "open-switch.lua") << R"lua(
local state = vim.fn.fnamemodify('open-state', ':p')
function _G.rv_test_open_state(first, second)
    local function line(win)
        if not vim.api.nvim_win_is_valid(win) then return '' end
        local buf = vim.api.nvim_win_get_buf(win)
        return vim.api.nvim_buf_get_lines(buf, 0, 1, false)[1] or ''
    end
    vim.fn.writefile({vim.fn.getcwd(), line(first), line(second)}, state)
end
vim.fn.writefile({'ready'}, 'switch-ready')
local stop = vim.uv.hrtime() + 3000000000
while not vim.uv.fs_stat('switch-release') and vim.uv.hrtime() < stop do
    vim.uv.sleep(5)
end
)lua";
    nvim.input("<Cmd>luafile open-switch.lua<CR>");
    const bool waiting = fixture.wait_for([&] {
        return fs::exists(fixture.root / "switch-ready");
    });
    RV_EDITOR_CHECK(waiting);
    if (!waiting || !click_review(fixture, "Discard All")) {
        std::ofstream(fixture.root / "switch-release") << "release\n";
        return;
    }
    rv_editor_shell_update(shell);
    fs::rename(target, fixture.root / "other-moved");
    std::ofstream(fixture.root / "switch-release") << "release\n";

    std::string review;
    RV_EDITOR_CHECK(fixture.wait_for([&] {
        rv_editor_shell_update(shell);
        fixture.draw(&review);
        return shell.app.project.root != fixture.root || includes(review, "Could not switch the code editor");
    }));
    RV_EDITOR_CHECK(shell.app.project.root == fixture.root);
    RV_EDITOR_CHECK(rv_editor_app_scene_dirty(shell.app));
    RV_EDITOR_CHECK(read_file(text) == "one\n");
    RV_EDITOR_CHECK(includes(review, "Could not switch the code editor"));

    // Ask Neovim directly about cwd and text so stale shell mirrors cannot
    // make a destroyed tab or a lost edit look as though it survived.
    nvim.input("<Cmd>lua rv_test_open_state(" + std::to_string(first_win) + ", " + std::to_string(second_win) + ")<CR>");
    const auto state = fixture.root / "open-state";
    std::string open_state;
    RV_EDITOR_CHECK(fixture.wait_for([&] {
        open_state = read_file(state);
        return std::ranges::count(open_state, '\n') == 3;
    }));
    RV_EDITOR_CHECK(open_state.starts_with(fixture.root.string() + "\n"));
    RV_EDITOR_CHECK(open_state.ends_with("\nunsaved-one\ntwo\n"));
    const auto *first_buffer = nvim.buffer_in(first_win);
    const auto *second_buffer = nvim.buffer_in(second_win);
    RV_EDITOR_CHECK(first_buffer != nullptr && first_buffer->name == text.string() && first_buffer->modified);
    RV_EDITOR_CHECK(second_buffer != nullptr && second_buffer->name == other_text.string() && !second_buffer->modified);
}

RV_EDITOR_TEST(rv_editor_test_project_quit_reviews_latest_changes_after_write_failure_until_discard)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto text = fixture.root / "first.txt";
    if (!open_text(fixture, text) || !edit_without_notification(fixture, "latest-")) {
        return;
    }
    RV_EDITOR_CHECK(shell.app.nvim.modified().empty());

    const bool allowed = rv_editor_shell_may_quit(shell);
    RV_EDITOR_CHECK(!allowed);
    if (allowed || !wait_for_review(fixture, leave::quit, "first.txt")) {
        return;
    }
    const auto saved = fixture.root / "saved.txt";
    fs::rename(text, saved);
    fs::create_directory(text);
    if (!click_review(fixture, "Save All")) {
        return;
    }
    RV_EDITOR_CHECK(fixture.wait_for([&] {
        rv_editor_shell_update(shell);
        return !shell.save_failed.empty();
    }));
    RV_EDITOR_CHECK(!shell.quit_now && shell.leaving == leave::quit);
    RV_EDITOR_CHECK(shell.app.nvim.modified().size() == 1);
    RV_EDITOR_CHECK(read_file(saved) == "one\n");
    std::string review;
    fixture.draw(&review);
    RV_EDITOR_CHECK(includes(review, "Not saved") && includes(review, "first.txt"));

    if (click_review(fixture, "Discard All")) {
        RV_EDITOR_CHECK(fixture.wait_for([&] {
            rv_editor_shell_update(shell);
            return shell.quit_now;
        }));
        RV_EDITOR_CHECK(read_file(saved) == "one\n");
    }
}

RV_EDITOR_TEST(rv_editor_test_project_quit_rechecks_code_changes_after_save_all)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto text = fixture.root / "first.txt";
    if (!open_text(fixture, text) || !fixture.modify_code(fixture.first, "saved-")) {
        return;
    }

    RV_EDITOR_CHECK(!rv_editor_shell_may_quit(shell));
    if (!wait_for_review(fixture, leave::quit, "first.txt")) {
        return;
    }
    // Edit again after the write, while the write result is queued for the shell.
    fixture.poll_nvim = false;
    if (!click_review(fixture, "Save All")) {
        return;
    }
    const bool wrote = fixture.wait_for([&] {
        return read_file(text) == "saved-one\n";
    });
    RV_EDITOR_CHECK(wrote);
    if (!wrote || !edit_without_notification(fixture, "late-")) {
        return;
    }
    fixture.poll_nvim = true;

    const bool replied = fixture.wait_for([&] {
        return !shell.saving;
    });
    RV_EDITOR_CHECK(replied);
    if (replied && wait_for_review(fixture, leave::quit, "first.txt") && click_review(fixture, "Save All")) {
        RV_EDITOR_CHECK(fixture.wait_for([&] {
            rv_editor_shell_update(shell);
            return shell.quit_now;
        }));
        RV_EDITOR_CHECK(read_file(text) == "late-saved-one\n");
    }
}

RV_EDITOR_TEST(rv_editor_test_project_return_invalidates_save_reply_for_the_next_quit_request)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto text = fixture.root / "first.txt";
    if (!open_text(fixture, text) || !fixture.modify_code(fixture.first, "saved-")) {
        return;
    }

    RV_EDITOR_CHECK(!rv_editor_shell_may_quit(shell));
    if (!wait_for_review(fixture, leave::quit, "first.txt")) {
        return;
    }
    // Keep the first write reply queued while the user returns and requests
    // Quit again. The second request has its own unsaved scene edit.
    fixture.poll_nvim = false;
    if (!click_review(fixture, "Save All") || !click_review(fixture, "Return")) {
        return;
    }
    RV_EDITOR_CHECK(!shell.quit_now && shell.leaving == leave::none);
    fixture.edit_scene();
    RV_EDITOR_CHECK(!rv_editor_shell_may_quit(shell));
    fixture.poll_nvim = true;

    const bool wrote = fixture.wait_for([&] {
        return !shell.saving && read_file(text) == "saved-one\n";
    });
    RV_EDITOR_CHECK(wrote);
    if (!wrote) {
        return;
    }
    rv_editor_shell_update(shell);
    RV_EDITOR_CHECK(!shell.quit_now);
    if (!shell.quit_now && wait_for_review(fixture, leave::quit, "courtyard.scene.toml")) {
        RV_EDITOR_CHECK(rv_editor_app_scene_dirty(shell.app));
        RV_EDITOR_CHECK(!fs::exists(fixture.scene_path));
        if (click_review(fixture, "Save All")) {
            RV_EDITOR_CHECK(fixture.wait_for([&] {
                rv_editor_shell_update(shell);
                return shell.quit_now;
            }));
            RV_EDITOR_CHECK(includes(read_file(fixture.scene_path), "tree"));
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_project_quit_return_wins_over_a_ready_clean_check)
{
    bool clean_reply = false;
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto text = fixture.root / "first.txt";
    if (!open_text(fixture, text) || !fixture.modify_code(fixture.first, "saved-")) {
        return;
    }
    RV_EDITOR_CHECK(!rv_editor_shell_may_quit(shell));
    if (!wait_for_review(fixture, leave::quit, "first.txt") || !click_review(fixture, "Save All")) {
        return;
    }
    const bool saved = fixture.wait_for([&] {
        return !shell.saving && read_file(text) == "saved-one\n";
    });
    RV_EDITOR_CHECK(saved);
    if (!saved) {
        return;
    }
    const auto button = fixture.find("Return");
    RV_EDITOR_CHECK(button.has_value());
    if (!button) {
        return;
    }
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(button->x, button->y);
    fixture.draw();
    io.AddMouseButtonEvent(0, true);
    fixture.draw();

    // Dispatch the post-save check. A second check on the same RPC channel
    // proves its clean answer arrived before we process the mouse-up frame.
    rv_editor_shell_update(shell);
    rv_editor_shell_after_save(shell);
    shell.app.nvim.check_modified([&](bool dirty, const std::string &error) {
        clean_reply = !dirty && error.empty();
    });
    const bool replied = fixture.wait_for([&] {
        return clean_reply;
    });
    RV_EDITOR_CHECK(replied);
    if (!replied) {
        return;
    }
    io.AddMouseButtonEvent(0, false);
    rv_editor_shell_update(shell);
    fixture.draw();
    rv_editor_shell_after_save(shell);

    RV_EDITOR_CHECK(!shell.quit_now);
    RV_EDITOR_CHECK(shell.leaving == leave::none);
    RV_EDITOR_CHECK(shell.app.project.root == fixture.root);
}

RV_EDITOR_TEST(rv_editor_test_project_open_cannot_redirect_a_code_tile_relative_write)
{
    bool checked = false;
    menu_fixture fixture;
    auto &shell = fixture.shell;
    auto &nvim = shell.app.nvim;
    const scoped_recent_config config(fixture.root / "config");
    const auto text = fixture.root / "code-tile-relative-write.txt";
    const auto target = make_project(fixture.root / "other");
    if (!open_text(fixture, text)) {
        return;
    }

    const auto draw_code = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(800, 300));
        ImGui::Begin("test code tile", nullptr, ImGuiWindowFlags_NoSavedSettings);
        rv_editor_shell_pane(&shell, fixture.first, rv_editor_pane_kind::code, rv_editor_theme_olive);
        ImGui::End();
        ImGui::EndFrame();
    };
    const auto write_in_code = [&](const char *command) {
        auto &io = ImGui::GetIO();
        io.AddInputCharactersUTF8(command);
        draw_code();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        draw_code();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        draw_code();
    };

    nvim.toggle_vim_mode();
    RV_EDITOR_CHECK(fixture.wait_for([&] {
        draw_code();
        return nvim.vim_mode();
    }));
    ImGui::GetIO().AddMousePosEvent(400, 150);
    draw_code();
    ImGuiWindow *code_editor = ImGui::GetCurrentContext()->HoveredWindow;
    RV_EDITOR_CHECK(code_editor != nullptr);
    if (code_editor == nullptr) {
        return;
    }
    ImGui::FocusWindow(code_editor);
    write_in_code(":write baseline.txt");
    const bool baseline = fixture.wait_for([&] {
        return read_file(fixture.root / "baseline.txt") == "one\n";
    });
    RV_EDITOR_CHECK(baseline);
    if (!baseline) {
        return;
    }

    // Observe a later RPC on Neovim's side without letting the shell consume
    // the Open reply. Absolute marker paths work under either working directory.
    std::ofstream(fixture.root / "open-fence.lua") << R"lua(
local ready = vim.fn.fnamemodify('fence-ready', ':p')
local dispatched = vim.fn.fnamemodify('open-dispatched', ':p')
local typed = vim.fn.fnamemodify('keys-dispatched', ':p')
local timer = vim.uv.new_timer()
timer:start(0, 5, vim.schedule_wrap(function()
    local phase = vim.g.rv_clipboard[1]
    if phase == 'open dispatched' and not vim.uv.fs_stat(dispatched) then
        vim.fn.writefile({'ready'}, dispatched)
    elseif phase == 'keys dispatched' and not timer:is_closing() then
        timer:close()
        vim.defer_fn(function() vim.fn.writefile({'ready'}, typed) end, 50)
    end
end))
vim.fn.writefile({'ready'}, ready)
)lua";
    nvim.input("<Cmd>luafile open-fence.lua<CR>");
    const bool fence_ready = fixture.wait_for([&] {
        return fs::exists(fixture.root / "fence-ready");
    });
    RV_EDITOR_CHECK(fence_ready);
    if (!fence_ready) {
        return;
    }

    rv_editor_shell_request_open(shell, target);
    rv_editor_shell_after_save(shell);
    nvim.check_modified([&](bool dirty, const std::string &error) {
        checked = !dirty && error.empty();
    });
    const bool clean = fixture.wait_for([&] {
        return checked;
    });
    RV_EDITOR_CHECK(clean);
    if (!clean) {
        return;
    }

    rv_editor_shell_after_save(shell);
    fixture.poll_nvim = false;
    nvim.set_clipboard("open dispatched");
    const bool dispatched = fixture.wait_for([&] {
        return fs::exists(fixture.root / "open-dispatched");
    });
    RV_EDITOR_CHECK(dispatched);
    RV_EDITOR_CHECK(shell.app.project.root == fixture.root);
    if (!dispatched) {
        return;
    }

    write_in_code(":write relative-copy.txt");
    nvim.set_clipboard("keys dispatched");
    RV_EDITOR_CHECK(fixture.wait_for([&] {
        return fs::exists(fixture.root / "keys-dispatched");
    }));
    RV_EDITOR_CHECK(!fs::exists(target / "relative-copy.txt"));
}
