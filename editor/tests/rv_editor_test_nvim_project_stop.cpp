#include <set>

#include "rv_editor_test_document_menu.hpp"

namespace
{

void check_project_stopping(menu_fixture &fixture, int64_t first_window, const fs::path &first, const fs::path &second)
{
    auto &nvim = fixture.shell.app.nvim;
    RV_EDITOR_CHECK(!nvim.running() && nvim.stopping());
    RV_EDITOR_CHECK(nvim.modified().empty());
    RV_EDITOR_CHECK(nvim.buffers().empty());
    RV_EDITOR_CHECK(nvim.buffer_in(first_window) == nullptr);
    RV_EDITOR_CHECK(nvim.window_for(fixture.first) == 0);
    RV_EDITOR_CHECK(read_file(first) == "one\n");
    RV_EDITOR_CHECK(read_file(second) == "two\n");
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_nvim_project_stop_clears_old_buffers_and_allows_a_new_client)
{
    menu_fixture fixture;
    auto &nvim = fixture.shell.app.nvim;
    auto &log = fixture.shell.app.log;
    const auto first = fixture.root / "first.txt";
    const auto second = fixture.root / "second.txt";
    const auto next_root = fixture.root / "next-project";
    fs::create_directory(next_root);
    const auto next = next_root / "next.txt";
    std::ofstream(next) << "next\n";
    if (!fixture.open_modified_pair(first, second)) {
        return;
    }
    const int64_t first_window = nvim.window_for(fixture.first);
    RV_EDITOR_CHECK(nvim.modified().size() == 2);

    std::string error;
    RV_EDITOR_CHECK(nvim.begin_stop_project(error) == RV_OK);
    check_project_stopping(fixture, first_window, first, second);

    const int premature_start = nvim.start(next_root, log, error);
    RV_EDITOR_CHECK(premature_start == RV_ERR_BUSY);
    if (premature_start != RV_ERR_BUSY) {
        return;
    }
    const bool stopped = fixture.wait_for([&] {
        return !nvim.stopping();
    });
    RV_EDITOR_CHECK(stopped && !nvim.running());
    if (!stopped) {
        return;
    }
    const int restarted = nvim.start(next_root, log, error);
    RV_EDITOR_CHECK(restarted == RV_OK);
    if (restarted != RV_OK || !fixture.open_code(fixture.first, next)) {
        return;
    }
    RV_EDITOR_CHECK(nvim.modified().empty());
    RV_EDITOR_CHECK(std::ranges::none_of(nvim.buffers(), [&](const auto &buffer) {
        return buffer.name == first.string() || buffer.name == second.string();
    }));
}

RV_EDITOR_TEST(rv_editor_test_nvim_project_stop_keeps_frames_responsive_with_a_slow_client)
{
    menu_fixture fixture;
    auto &nvim = fixture.shell.app.nvim;
    auto &log = fixture.shell.app.log;
    const auto first = fixture.root / "first.txt";
    std::ofstream(first) << "one\n";
    if (!fixture.start_code() || !fixture.open_code(fixture.first, first)) {
        return;
    }

    // Tell the test when Neovim begins a blocking call that prevents it from handling the quit request.
    nvim.input("<Cmd>lua vim.fn.writefile({'ready'}, 'sleep-ready'); vim.loop.sleep(5000)<CR>");
    const bool sleep_started = fixture.wait_for([&] {
        return fs::exists(fixture.root / "sleep-ready");
    });
    RV_EDITOR_CHECK(sleep_started);
    if (!sleep_started) {
        return;
    }

    std::string error;
    const auto stop_start = std::chrono::steady_clock::now();
    RV_EDITOR_CHECK(nvim.begin_stop_project(error) == RV_OK);
    RV_EDITOR_CHECK(std::chrono::steady_clock::now() - stop_start < 1s);
    RV_EDITOR_CHECK(!nvim.running() && nvim.stopping());

    const auto frame_start = std::chrono::steady_clock::now();
    nvim.update(log);
    RV_EDITOR_CHECK(std::chrono::steady_clock::now() - frame_start < 1s);
    RV_EDITOR_CHECK(nvim.stopping());
    const bool stopped = fixture.wait_for([&] {
        return !nvim.stopping();
    });
    RV_EDITOR_CHECK(stopped && !nvim.running());
    if (stopped) {
        RV_EDITOR_CHECK(nvim.start(fixture.root, log, error) == RV_OK);
    }
}

namespace
{

std::set<std::string> open_pipe_targets()
{
    std::set<std::string> pipes;
    for (const auto &descriptor : fs::directory_iterator("/proc/self/fd")) {
        const std::string target = fs::read_symlink(descriptor.path()).string();
        if (target.starts_with("pipe:[")) {
            pipes.insert(target);
        }
    }
    return pipes;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_nvim_project_stop_releases_its_pipes)
{
    menu_fixture fixture;
    auto &nvim = fixture.shell.app.nvim;
    std::string error;
    // The process guard keeps its own pipe open for the editor's lifetime.
    const int guard = rv_editor_guard_start(error);
    RV_EDITOR_CHECK(guard == RV_OK || guard == RV_ERR_BUSY);
    if (guard != RV_OK && guard != RV_ERR_BUSY) {
        return;
    }
    const auto baseline = open_pipe_targets();
    if (!fixture.start_code()) {
        return;
    }
    auto client_pipes = open_pipe_targets();
    for (const auto &pipe : baseline) {
        client_pipes.erase(pipe);
    }
    RV_EDITOR_CHECK(!client_pipes.empty());

    RV_EDITOR_CHECK(nvim.begin_stop_project(error) == RV_OK);
    const bool stopped = fixture.wait_for([&] {
        return !nvim.stopping();
    });
    RV_EDITOR_CHECK(stopped && !nvim.running());
    if (!stopped) {
        return;
    }

    const auto remaining_pipes = open_pipe_targets();
    RV_EDITOR_CHECK(std::ranges::none_of(client_pipes, [&](const auto &pipe) {
        return remaining_pipes.contains(pipe);
    }));
}

RV_EDITOR_TEST(rv_editor_test_project_lifecycle_close_shows_picker_while_neovim_drains)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    auto &nvim = shell.app.nvim;
    const auto first = fixture.root / "first.txt";
    std::ofstream(first) << "first\n";
    if (!fixture.start_code() || !fixture.open_code(fixture.first, first)) {
        return;
    }

    // Keep the client responsive for the unsaved-document check and delay its exit after authorization.
    nvim.input("<Cmd>lua vim.api.nvim_create_autocmd('VimLeavePre', {once = true, callback = function() "
               "vim.fn.writefile({'draining'}, 'exit-started'); vim.loop.sleep(1500) end}); "
               "vim.fn.writefile({'ready'}, 'exit-hook-ready')<CR>");
    const bool hook_ready = fixture.wait_for([&] {
        return fs::exists(fixture.root / "exit-hook-ready");
    });
    RV_EDITOR_CHECK(hook_ready);
    if (!hook_ready) {
        return;
    }

    shell.start_page = rv_editor_start_page::manual;
    const auto close_started = std::chrono::steady_clock::now();
    rv_editor_shell_request_close_project(shell);
    fixture.poll_nvim = false;
    const auto frame = [&] {
        rv_editor_shell_update(shell, true);
    };
    const bool showed_picker = fixture.wait_for([&] {
        frame();
        return !shell.app.project.open &&
            (shell.start_page == rv_editor_start_page::recent || shell.start_page == rv_editor_start_page::open_project);
    });
    RV_EDITOR_CHECK(showed_picker);
    if (!showed_picker) {
        return;
    }
    RV_EDITOR_CHECK(std::chrono::steady_clock::now() - close_started < 1s);
    RV_EDITOR_CHECK(nvim.stopping() && !nvim.running());
    RV_EDITOR_CHECK(fixture.wait_for([&] {
        frame();
        return fs::exists(fixture.root / "exit-started");
    }));
    RV_EDITOR_CHECK(nvim.stopping());
    const bool stopped = fixture.wait_for([&] {
        frame();
        return !nvim.stopping();
    });
    RV_EDITOR_CHECK(stopped);
    if (!stopped) {
        return;
    }

    const auto next = fixture.root / "next-project";
    fs::create_directory(next);
    std::ofstream(next / "disc.toml") << "[disc]\nid = \"next\"\ntitle = \"Next Project\"\n";
    rv_editor_shell_request_open(shell, next);
    frame();
    RV_EDITOR_CHECK(shell.app.project.open && shell.app.project.root == next);
    std::string error;
    RV_EDITOR_CHECK(nvim.start(next, shell.app.log, error) == RV_OK);
}
