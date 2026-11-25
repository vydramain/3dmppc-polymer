#include "rv_editor_test_document_menu.hpp"

RV_EDITOR_TEST(rv_editor_test_document_menu_save_as_does_not_block_save_after_code_tile_changes_kind)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto first = fixture.root / "first.txt";
    const auto copy = fixture.root / "copy.txt";
    std::ofstream(first) << "one\n";
    if (!fixture.start_code() || !fixture.open_code(fixture.first, first)) {
        return;
    }
    fixture.focus(fixture.first);
    const auto save_as = fixture.open_item({ "Save As...", "Save As…" }, "File");
    if (!save_as) {
        return;
    }
    fixture.click(*save_as);
    RV_EDITOR_CHECK(fixture.find("Cancel").has_value());
    std::snprintf(shell.save_as_path, sizeof(shell.save_as_path), "%s", copy.string().c_str());

    shell.ws.pending.what = rv_editor_tile_action::op::set_kind;
    shell.ws.pending.pane = fixture.first;
    shell.ws.pending.kind = rv_editor_pane_kind::empty;
    fixture.draw();
    RV_EDITOR_CHECK(shell.ws.panes.panes[fixture.first].kind == rv_editor_pane_kind::empty);
    RV_EDITOR_CHECK(rv_editor_tile_find(shell.ws.layout, fixture.first) != rv_editor_tile_none);
    rv_editor_shell_update(shell);

    fixture.edit_scene();
    fixture.focus(fixture.scene);
    const auto save = fixture.open_item({ "Save" }, "File");
    if (save) {
        fixture.click(*save);
        RV_EDITOR_CHECK(includes(read_file(fixture.scene_path), "tree"));
    }
    RV_EDITOR_CHECK(read_file(first) == "one\n" && !fs::exists(copy));
}

RV_EDITOR_TEST(rv_editor_test_document_menu_save_reports_a_lost_nvim_reply_and_releases_other_saves)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto first = fixture.root / "first.txt";
    std::ofstream(first) << "one\n";
    if (!fixture.start_code() || !fixture.open_code(fixture.first, first) || !fixture.modify_code(fixture.first, "alpha-")) {
        return;
    }
    // End this test's nvim before the write can reply. The marker confirms the hook was installed before Save.
    shell.app.nvim.input("<Cmd>lua vim.api.nvim_create_autocmd('BufWritePre', {callback = function() "
                         "vim.loop.kill(vim.fn.getpid(), 9) end}); vim.fn.writefile({'ready'}, 'write-hook-ready')<CR>");
    const bool hook_ready = fixture.wait_for([&] {
        return fs::exists(fixture.root / "write-hook-ready");
    });
    RV_EDITOR_CHECK(hook_ready);
    if (!hook_ready) {
        return;
    }

    fixture.focus(fixture.first);
    const auto save = fixture.open_item({ "Save" }, "File");
    if (!save) {
        return;
    }
    fixture.click(*save);
    const bool disconnected = fixture.wait_for([&] {
        rv_editor_shell_update(shell);
        return !shell.app.nvim.running() && !shell.app.nvim.stopping();
    });
    RV_EDITOR_CHECK(disconnected);
    if (!disconnected) {
        return;
    }
    RV_EDITOR_CHECK(read_file(first) == "one\n");
    RV_EDITOR_CHECK(std::ranges::any_of(shell.save_failed, [](const auto &failure) {
        return !failure.ok && !failure.error.empty();
    }));

    fixture.edit_scene();
    fixture.focus(fixture.scene);
    const auto save_scene = fixture.open_item({ "Save" }, "File");
    if (save_scene) {
        fixture.click(*save_scene);
        RV_EDITOR_CHECK(includes(read_file(fixture.scene_path), "tree"));
    }
}

RV_EDITOR_TEST(rv_editor_test_document_menu_save_failure_can_be_retried_from_review_changes)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto first = fixture.root / "first.txt";
    std::ofstream(first) << "one\n";
    if (!fixture.start_code() || !fixture.open_code(fixture.first, first) || !fixture.modify_code(fixture.first, "alpha-")) {
        return;
    }
    fs::remove(first);
    fs::create_directory(first);
    fixture.focus(fixture.first);
    const auto save = fixture.open_item({ "Save" }, "File");
    if (!save) {
        return;
    }
    fixture.click(*save);
    const bool failed = fixture.wait_for([&] {
        return !shell.saving && !shell.save_failed.empty();
    });
    RV_EDITOR_CHECK(failed);
    if (!failed) {
        return;
    }
    fs::remove(first);

    shell.app.unsaved_ask = rv_editor_unsaved_ask::build;
    rv_editor_shell_update(shell);
    const auto retry = fixture.find("Save and Build");
    RV_EDITOR_CHECK(retry.has_value());
    if (retry) {
        fixture.click(*retry);
        RV_EDITOR_CHECK(fixture.wait_for([&] {
            return !shell.saving && read_file(first) == "alpha-one\n";
        }));
    }
}

RV_EDITOR_TEST(rv_editor_test_document_menu_review_save_all_retry_with_nvim_unavailable_keeps_quit_pending)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto first = fixture.root / "first.txt";
    std::ofstream(first) << "one\n";
    if (!fixture.start_code() || !fixture.open_code(fixture.first, first) || !fixture.modify_code(fixture.first, "alpha-")) {
        return;
    }
    // End this fixture's Neovim before the write can reply.
    shell.app.nvim.input("<Cmd>lua vim.api.nvim_create_autocmd('BufWritePre', {callback = function() "
                         "vim.loop.kill(vim.fn.getpid(), 9) end}); vim.fn.writefile({'ready'}, 'write-hook-ready')<CR>");
    const bool hook_ready = fixture.wait_for([&] {
        return fs::exists(fixture.root / "write-hook-ready");
    });
    RV_EDITOR_CHECK(hook_ready);
    if (!hook_ready) {
        return;
    }

    RV_EDITOR_CHECK(!rv_editor_shell_may_quit(shell));
    rv_editor_shell_update(shell);
    const auto save_all = fixture.find("Save All");
    RV_EDITOR_CHECK(save_all.has_value());
    if (!save_all) {
        return;
    }
    fixture.click(*save_all);
    const bool disconnected = fixture.wait_for([&] {
        rv_editor_shell_update(shell);
        return !shell.app.nvim.running() && !shell.app.nvim.stopping() && !shell.saving;
    });
    RV_EDITOR_CHECK(disconnected);
    if (!disconnected) {
        return;
    }
    RV_EDITOR_CHECK(!shell.quit_now && shell.leaving == rv_editor_shell::rv_editor_leave::quit);
    RV_EDITOR_CHECK(!shell.save_failed.empty());
    RV_EDITOR_CHECK(read_file(first) == "one\n");

    const auto retry = fixture.find("Save All");
    RV_EDITOR_CHECK(retry.has_value());
    if (!retry) {
        return;
    }
    fixture.click(*retry);
    rv_editor_shell_update(shell);

    RV_EDITOR_CHECK(read_file(first) == "one\n");
    RV_EDITOR_CHECK(std::ranges::any_of(shell.app.nvim.modified(), [&](const auto &buffer) {
        return buffer.name == first.string();
    }));
    RV_EDITOR_CHECK(!shell.quit_now && shell.leaving == rv_editor_shell::rv_editor_leave::quit);
    std::string review;
    fixture.draw(&review);
    RV_EDITOR_CHECK(includes(review, "Not saved"));
    RV_EDITOR_CHECK(std::ranges::any_of(shell.save_failed, [&](const auto &failure) {
        return !failure.ok && !failure.error.empty() && includes(review, failure.error);
    }));
}
