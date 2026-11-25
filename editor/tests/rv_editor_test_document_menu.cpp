#include "rv_editor_test_document_menu.hpp"

RV_EDITOR_TEST(rv_editor_test_document_menu_save_targets_scene)
{
    menu_fixture fixture;
    fixture.edit_scene();
    const auto save = fixture.open_item({ "Save" }, "File");
    if (!save) {
        return;
    }
    RV_EDITOR_CHECK(includes(fixture.hover(*save), "courtyard"));
    fixture.click(*save);
    RV_EDITOR_CHECK(!fixture.shell.app.scene->dirty);
    RV_EDITOR_CHECK(includes(read_file(fixture.scene_path), "tree"));
}

RV_EDITOR_TEST(rv_editor_test_document_menu_restores_scene_history_and_explains_empty_history)
{
    menu_fixture fixture;
    const auto no_undo = fixture.open_item({ "Undo" }, "Edit");
    if (!no_undo) {
        return;
    }
    const auto undo_reason = fixture.hover(*no_undo);
    RV_EDITOR_CHECK(includes(undo_reason, "nothing to undo") || includes(undo_reason, "Nothing to undo"));

    fixture.edit_scene();
    const auto undo = fixture.open_item({ "Undo" }, "Edit");
    if (!undo) {
        return;
    }
    RV_EDITOR_CHECK(includes(fixture.hover(*undo), "courtyard"));
    fixture.click(*undo);
    RV_EDITOR_CHECK(fixture.shell.app.scene->scene.objects.empty());

    const auto redo = fixture.open_item({ "Redo" }, "Edit");
    if (!redo) {
        return;
    }
    RV_EDITOR_CHECK(includes(fixture.hover(*redo), "courtyard"));
    fixture.click(*redo);
    RV_EDITOR_CHECK(fixture.shell.app.scene->scene.objects.size() == 1);

    const auto no_redo = fixture.open_item({ "Redo" }, "Edit");
    if (no_redo) {
        const auto redo_reason = fixture.hover(*no_redo);
        RV_EDITOR_CHECK(includes(redo_reason, "nothing to redo") || includes(redo_reason, "Nothing to redo"));
    }
}

RV_EDITOR_TEST(rv_editor_test_document_menu_save_switches_between_two_code_buffers)
{
    menu_fixture fixture;
    const auto first = fixture.root / "first.txt";
    const auto second = fixture.root / "second.txt";
    if (!fixture.open_modified_pair(first, second)) {
        return;
    }
    fixture.edit_scene();

    fixture.focus(fixture.first);
    const auto save_first = fixture.open_item({ "Save" }, "File");
    if (!save_first) {
        return;
    }
    RV_EDITOR_CHECK(includes(fixture.hover(*save_first), "first.txt"));
    // Focus can move while the menu is open. The displayed target remains the document the action uses.
    fixture.focus(fixture.second);
    fixture.click(*save_first);
    RV_EDITOR_CHECK(fixture.wait_for([&] {
        return !fixture.shell.saving && read_file(first) == "alpha-one\n";
    }));
    RV_EDITOR_CHECK(read_file(second) == "two\n");

    fixture.focus(fixture.second);
    const auto save_second = fixture.open_item({ "Save" }, "File");
    if (!save_second) {
        return;
    }
    RV_EDITOR_CHECK(includes(fixture.hover(*save_second), "second.txt"));
    fixture.click(*save_second);
    RV_EDITOR_CHECK(fixture.wait_for([&] {
        return !fixture.shell.saving && read_file(second) == "beta-two\n";
    }));
    RV_EDITOR_CHECK(fixture.shell.app.scene->dirty);

    fixture.focus(fixture.scene);
    const auto save_scene = fixture.open_item({ "Save" }, "File");
    if (save_scene) {
        RV_EDITOR_CHECK(includes(fixture.hover(*save_scene), "courtyard"));
        fixture.click(*save_scene);
        RV_EDITOR_CHECK(includes(read_file(fixture.scene_path), "tree"));
        RV_EDITOR_CHECK(read_file(first) == "alpha-one\n" && read_file(second) == "beta-two\n");
    }
}

RV_EDITOR_TEST(rv_editor_test_document_menu_uses_the_selected_code_buffer_history)
{
    menu_fixture fixture;
    auto &nvim = fixture.shell.app.nvim;
    const auto first = fixture.root / "first.txt";
    const auto second = fixture.root / "second.txt";
    if (!fixture.open_modified_pair(first, second)) {
        return;
    }
    fixture.edit_scene();
    const auto modified = [&](rv_editor_pane_id pane) {
        const auto *buffer = nvim.buffer_in(nvim.window_for(pane));
        return buffer != nullptr && buffer->modified;
    };

    for (const auto pane : { fixture.first, fixture.second }) {
        const std::string name = pane == fixture.first ? first.filename().string() : second.filename().string();
        const auto other = pane == fixture.first ? fixture.second : fixture.first;
        fixture.focus(pane);
        const auto undo = fixture.open_item({ "Undo" }, "Edit");
        if (!undo) {
            return;
        }
        RV_EDITOR_CHECK(includes(fixture.hover(*undo), name));
        fixture.click(*undo);
        RV_EDITOR_CHECK(fixture.wait_for([&] {
            return !modified(pane);
        }));
        RV_EDITOR_CHECK(modified(other));

        const auto no_undo = fixture.open_item({ "Undo" }, "Edit");
        if (!no_undo) {
            return;
        }
        const auto why_no_undo = fixture.hover(*no_undo);
        RV_EDITOR_CHECK(includes(why_no_undo, "nothing to undo") || includes(why_no_undo, "Nothing to undo"));

        const auto redo = fixture.open_item({ "Redo" }, "Edit");
        if (!redo) {
            return;
        }
        RV_EDITOR_CHECK(includes(fixture.hover(*redo), name));
        fixture.click(*redo);
        RV_EDITOR_CHECK(fixture.wait_for([&] {
            return modified(pane);
        }));
        RV_EDITOR_CHECK(modified(other));
    }
    RV_EDITOR_CHECK(fixture.shell.app.scene->scene.objects.size() == 1);
}

RV_EDITOR_TEST(rv_editor_test_document_menu_save_all_explains_scope_and_reports_buffer_failures)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto first = fixture.root / "first.txt";
    const auto blocked = fixture.root / "blocked.txt";
    if (!fixture.open_modified_pair(first, blocked)) {
        return;
    }
    // A directory where a file must be written fails even when running as root.
    fs::remove(blocked);
    fs::create_directory(blocked);
    const int64_t win = shell.app.nvim.window_for(fixture.second);
    shell.app.nvim.input("<Cmd>lua vim.api.nvim_set_current_win(" + std::to_string(win) +
        "); vim.cmd.enew(); vim.api.nvim_set_current_line('untitled')<CR>");
    const bool unnamed_modified = fixture.wait_for([&] {
        const auto *buffer = shell.app.nvim.buffer_in(win);
        return buffer != nullptr && buffer->name.empty() && buffer->modified;
    });
    RV_EDITOR_CHECK(unnamed_modified);
    if (!unnamed_modified) {
        return;
    }
    fixture.edit_scene();

    const auto save = fixture.open_item({ "Save All" }, "File");
    if (!save) {
        return;
    }
    const auto explanation = fixture.hover(*save);
    RV_EDITOR_CHECK((includes(explanation, "files") || includes(explanation, "buffers")) && includes(explanation, "scene"));
    fixture.click(*save);
    RV_EDITOR_CHECK(fixture.wait_for([&] {
        return !shell.saving;
    }));
    RV_EDITOR_CHECK(read_file(first) == "alpha-one\n");
    RV_EDITOR_CHECK(includes(read_file(fixture.scene_path), "tree"));
    RV_EDITOR_CHECK(std::ranges::any_of(shell.save_failed, [](const auto &failure) {
        return failure.id != 0 && failure.name.empty() && !failure.error.empty();
    }));
    RV_EDITOR_CHECK(std::ranges::any_of(shell.save_failed, [&](const auto &failure) {
        return failure.name == blocked.string() && !failure.error.empty();
    }));
}

RV_EDITOR_TEST(rv_editor_test_document_menu_save_as_explains_target_and_allows_cancel_or_write)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    const auto first = fixture.root / "first.txt";
    const auto copy = fixture.root / "copy.txt";
    std::ofstream(first) << "one\n";
    if (!fixture.start_code() || !fixture.open_code(fixture.first, first) || !fixture.modify_code(fixture.first, "alpha-")) {
        return;
    }

    fixture.focus(fixture.scene);
    const auto disabled = fixture.open_item({ "Save As...", "Save As…" }, "File");
    if (!disabled) {
        return;
    }
    const auto disabled_explanation = fixture.hover(*disabled);
    RV_EDITOR_CHECK(includes(disabled_explanation, "code buffer"));
    fixture.click(*disabled);
    RV_EDITOR_CHECK(shell.save_as_buffer == 0);

    fixture.focus(fixture.first);
    for (const char *answer : { "Cancel", "Save" }) {
        const auto item = fixture.open_item({ "Save As...", "Save As…" }, "File");
        if (!item) {
            return;
        }
        RV_EDITOR_CHECK(includes(fixture.hover(*item), "first.txt"));
        fixture.click(*item);
        RV_EDITOR_CHECK(shell.save_as_buffer != 0);
        if (shell.save_as_buffer == 0) {
            return;
        }
        std::snprintf(shell.save_as_path, sizeof(shell.save_as_path), "%s", copy.string().c_str());
        fixture.draw();
        const auto button = fixture.find(answer);
        RV_EDITOR_CHECK(button.has_value());
        if (!button) {
            return;
        }
        fixture.click(*button);
        RV_EDITOR_CHECK(fixture.wait_for([&] {
            return shell.save_as_buffer == 0;
        }));
        if (std::string_view(answer) == "Cancel") {
            RV_EDITOR_CHECK(!fs::exists(copy));
        }
        RV_EDITOR_CHECK(read_file(first) == "one\n");
    }
    RV_EDITOR_CHECK(read_file(copy) == "alpha-one\n");
}
