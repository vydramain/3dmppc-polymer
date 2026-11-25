// AC3 and AC8: open the actual dropdowns on the start screen and in the workspace.

#include "rv_editor_test_document_menu.hpp"

namespace
{

std::vector<std::string> menu_labels(menu_fixture &fixture, const char *menu)
{
    fixture.dismiss();
    const auto button = fixture.find(menu);
    RV_EDITOR_CHECK(button.has_value());
    if (!button) {
        return {};
    }
    fixture.click(*button);
    std::vector<std::string> labels;
    for (const auto &entry : fixture.draw().entries) {
        if (entry.y0 > button->y && entry.text_w > 0.0f) {
            labels.push_back(entry.label);
        }
    }
    return labels;
}

bool hover_submenu(menu_fixture &fixture, const char *label)
{
    const auto item = fixture.find(label);
    RV_EDITOR_CHECK(item.has_value());
    if (!item) {
        return false;
    }
    ImGui::GetIO().AddMousePosEvent(item->x, item->y);
    fixture.draw();
    fixture.draw();
    return true;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_main_menu_tree_on_start_and_in_project)
{
    menu_fixture fixture;
    const std::vector<std::string> project{ "New Project…",
        "Open Project…",
        "Open Recent",
        "Save",
        "Save All",
        "Save As…",
        "Undo",
        "Redo",
        "Project Settings…",
        "Close Project",
        "Quit" };
    const std::vector<std::string> editor{ "New Tile",
        "Layout",
        "Reset Current Layout",
        "UI Scale",
        "Main Font Size",
        "Code Font Size",
        "Editor Settings…" };
    const std::vector<std::string> help{ "Interactive Manual", "About 3dmppc Editor" };
    for (const bool open : { false, true }) {
        fixture.shell.app.project.open = open;
        fixture.dismiss();
        std::vector<std::string> expected{ "Project", "Editor", "Help" };
        if (open) {
            expected.insert(expected.end(), { "Code", "Scene", "Debug", "Burn" });
        }
        std::vector<std::string> bar;
        for (const auto &entry : fixture.draw().entries) {
            if (entry.text_w > 0.0f) {
                bar.push_back(entry.label);
            }
        }
        RV_EDITOR_CHECK(bar == expected);
        RV_EDITOR_CHECK(menu_labels(fixture, "Project") == project);
        RV_EDITOR_CHECK(menu_labels(fixture, "Editor") == editor);
        RV_EDITOR_CHECK(menu_labels(fixture, "Help") == help);
    }
}

RV_EDITOR_TEST(rv_editor_test_main_menu_creates_selected_tile_and_resets_only_active_layout)
{
    menu_fixture fixture;
    auto &shell = fixture.shell;
    rv_editor_shell_reset_layout(shell, shell.active);
    const size_t code_preset_nodes = shell.ws.layout.nodes.size();
    menu_labels(fixture, "Editor");
    if (!hover_submenu(fixture, "New Tile")) {
        return;
    }
    const auto add = fixture.find("Console Output");
    RV_EDITOR_CHECK(add.has_value());
    if (!add) {
        return;
    }
    fixture.click(*add);
    RV_EDITOR_CHECK(shell.ws.layout.nodes.size() > code_preset_nodes);
    if (shell.ws.focused_leaf >= shell.ws.layout.nodes.size()) {
        RV_EDITOR_CHECK(false && "new tile has focus");
        return;
    }
    const auto &leaf = shell.ws.layout.nodes[shell.ws.focused_leaf].leaf;
    RV_EDITOR_CHECK(shell.ws.panes.panes[leaf.tabs.front()].kind == rv_editor_pane_kind::output);
    const size_t edited_code_nodes = shell.ws.layout.nodes.size();

    rv_editor_shell_switch(shell, rv_editor_layout_preset::scene);
    rv_editor_shell_reset_layout(shell, shell.active);
    const size_t scene_preset_nodes = shell.ws.layout.nodes.size();
    rv_editor_shell_new_tile(shell, rv_editor_pane_kind::terminal);
    RV_EDITOR_CHECK(shell.ws.layout.nodes.size() > scene_preset_nodes);
    menu_labels(fixture, "Editor");
    const auto reset = fixture.find("Reset Current Layout");
    RV_EDITOR_CHECK(reset.has_value());
    if (!reset) {
        return;
    }
    fixture.click(*reset);
    RV_EDITOR_CHECK(shell.ws.layout.nodes.size() == scene_preset_nodes);
    rv_editor_shell_switch(shell, rv_editor_layout_preset::code);
    RV_EDITOR_CHECK(shell.ws.layout.nodes.size() == edited_code_nodes);
}
