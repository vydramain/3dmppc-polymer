// Files: the project tree and the file operations on it.

#include <cmath>
#include <cstring>
#include <string>

#include "imgui.h"

#include "panes/rv_editor_panes.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

const char *rv_editor_files_dialog_title(rv_editor_files_view::rv_editor_files_dialog dialog)
{
    switch (dialog) {
        case rv_editor_files_view::rv_editor_files_dialog::new_file: return "New File";
        case rv_editor_files_view::rv_editor_files_dialog::new_dir: return "New Folder";
        case rv_editor_files_view::rv_editor_files_dialog::rename: return "Rename";
        case rv_editor_files_view::rv_editor_files_dialog::remove: return "Delete";
        default: return "";
    }
}

// The directory a new entry goes into: the selection when it is a directory,
// its parent when it is a file, the root otherwise.
std::filesystem::path rv_editor_files_target_dir(rv_editor_app &app)
{
    const std::filesystem::path &sel = app.files.selected;
    if (sel.empty()) {
        return app.files.root().path;
    }
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::symlink_status(sel, ec)) ? sel : sel.parent_path();
}

void rv_editor_files_ask(rv_editor_files_view &view, rv_editor_files_view::rv_editor_files_dialog dialog,
    const std::filesystem::path &target, const std::string &name)
{
    view.dialog = dialog;
    view.target = target;
    view.error.clear();
    std::memset(view.name, 0, sizeof(view.name));
    std::strncpy(view.name, name.c_str(), sizeof(view.name) - 1);
    view.opening = true;
}

void rv_editor_files_menu(rv_editor_app &app, rv_editor_files_view &view, const rv_editor_file_node &node)
{
    if (ImGui::MenuItem("New File")) {
        app.files.selected = node.path;
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::new_file,
            rv_editor_files_target_dir(app), "");
    }
    if (ImGui::MenuItem("New Folder")) {
        app.files.selected = node.path;
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::new_dir,
            rv_editor_files_target_dir(app), "");
    }
    const bool is_root = node.path == app.files.root().path;
    if (ImGui::MenuItem("Rename", nullptr, false, !is_root)) {
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::rename, node.path, node.name);
    }
    if (ImGui::MenuItem("Delete", nullptr, false, !is_root)) {
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::remove, node.path, node.name);
    }
}

// The letter and colour that stand in for a file's icon (editor/docs/icons.md).
void rv_editor_files_chip(const rv_editor_file_node &node, char &letter, uint32_t &color)
{
    if (node.symlink) {
        letter = '@';
        color = 0x6c7086;
        return;
    }
    if (node.dir) {
        letter = 'D';
        color = 0x958831;
        return;
    }
    const std::string ext = node.path.extension().string();
    struct kind
    {
        const char *ext;
        char letter;
        uint32_t color;
    };
    static constexpr kind kinds[] = { { ".lua", 'L', 0xcba6f7 }, { ".cpp", 'C', 0x89b4fa }, { ".c", 'C', 0x89b4fa },
        { ".cc", 'C', 0x89b4fa }, { ".hpp", 'H', 0x74c7ec }, { ".h", 'H', 0x74c7ec }, { ".toml", 'T', 0xfab387 },
        { ".png", 'I', 0xa6e3a1 }, { ".pcm", 'S', 0x94e2d5 }, { ".wav", 'S', 0x94e2d5 }, { ".md", 'D', 0xcdd6f4 },
        { ".txt", 'D', 0xcdd6f4 } };
    letter = 'F';
    color = 0xa6adc8;
    if (node.name == "disc.toml") {
        letter = 'M';
        color = 0xfab387;
        return;
    }
    for (const kind &k : kinds) {
        if (ext == k.ext) {
            letter = k.letter;
            color = k.color;
            return;
        }
    }
}

void rv_editor_files_node(rv_editor_app &app, rv_editor_files_view &view, rv_editor_file_node &node)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick;
    const bool branch = node.dir && !node.symlink;
    if (!branch) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (app.files.selected == node.path) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    // Two leading spaces keep a cell for the icon's stand-in and one for a gap.
    const std::string label = "  " + node.name + (node.symlink ? " ->" : "");
    ImGui::PushID(node.path.c_str());
    if (branch) {
        ImGui::SetNextItemOpen(node.expanded);
    }
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
    {
        char letter = 'F';
        uint32_t color = 0;
        rv_editor_files_chip(node, letter, color);
        // A row is one cell high: the stand-in is the letter itself, in its colour.
        const char text[2] = { letter, '\0' };
        const float cell = ImGui::GetTextLineHeight();
        const ImVec2 at(ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing(),
            std::floor((ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y - cell) / 2.0f));
        ImGui::GetWindowDrawList()->AddText(at, rv_editor_col(color), text);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        app.files.selected = node.path;
    }
    if (!branch && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        app.open_requests.push_back(node.path);
    }
    if (node.symlink && ImGui::IsItemHovered()) {
        std::error_code ec;
        ImGui::SetTooltip("link to %s", std::filesystem::read_symlink(node.path, ec).c_str());
    }
    rv_editor_menu_style_push();
    if (ImGui::BeginPopupContextItem("##menu")) {
        rv_editor_files_menu(app, view, node);
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();
    if (branch) {
        node.expanded = open;
        if (open) {
            if (!node.listed) {
                app.files.list(node);
            }
            for (rv_editor_file_node &child : node.children) {
                rv_editor_files_node(app, view, child);
            }
            ImGui::TreePop();
        }
    }
    ImGui::PopID();
}

void rv_editor_files_dialog(rv_editor_app &app, rv_editor_files_view &view, const rv_editor_theme &theme)
{
    using dialog_kind = rv_editor_files_view::rv_editor_files_dialog;
    if (view.dialog == dialog_kind::none) {
        return;
    }
    const char *title = rv_editor_files_dialog_title(view.dialog);
    if (view.opening) {
        ImGui::OpenPopup(title);
        view.opening = false;
    }
    if (!rv_editor_dialog_begin(title, theme)) {
        view.dialog = dialog_kind::none;
        return;
    }

    const std::string what = view.target.filename().string();
    bool confirm = false;
    if (view.dialog == dialog_kind::remove) {
        std::error_code ec;
        const bool dir = std::filesystem::is_directory(std::filesystem::symlink_status(view.target, ec));
        ImGui::Text("Delete %s%s? This cannot be undone.", what.c_str(), dir ? " and everything in it" : "");
    } else {
        ImGui::Text("%s in %s", view.dialog == dialog_kind::rename ? "New name" : "Name",
            (view.dialog == dialog_kind::rename ? view.target.parent_path() : view.target).c_str());
        rv_editor_field field;
        field.invalid = view.error.empty() ? nullptr : view.error.c_str();
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24.0f);
        rv_editor_text_field("##name", view.name, sizeof(view.name), theme, field);
        confirm = ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    }
    if (!view.error.empty()) {
        ImGui::TextWrapped("%s", view.error.c_str());
    }
    confirm = rv_editor_button(view.dialog == dialog_kind::remove ? "Delete" : "OK", theme) || confirm;
    ImGui::SameLine();
    const bool cancel = rv_editor_button("Cancel", theme) || ImGui::IsKeyPressed(ImGuiKey_Escape);

    if (confirm) {
        bool ok = false;
        const std::string name = view.name;
        switch (view.dialog) {
            case dialog_kind::new_file: ok = app.files.create_file(view.target, name, view.error); break;
            case dialog_kind::new_dir: ok = app.files.create_dir(view.target, name, view.error); break;
            case dialog_kind::rename: ok = rv_editor_app_rename(app, view.target, name, view.error); break;
            case dialog_kind::remove: ok = rv_editor_app_remove(app, view.target, view.error); break;
            default: break;
        }
        if (ok) {
            if (view.dialog == dialog_kind::new_file) {
                app.files.selected = view.target / name;
                app.open_requests.push_back(view.target / name);
            }
            view.dialog = dialog_kind::none;
            ImGui::CloseCurrentPopup();
        }
    } else if (cancel) {
        view.dialog = dialog_kind::none;
        ImGui::CloseCurrentPopup();
    }
    rv_editor_dialog_end();
}

} // namespace

void rv_editor_pane_files(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    if (!app.files.is_open()) {
        ImGui::TextWrapped("No project is open: File > Open Folder.");
        return;
    }
    rv_editor_files_view &view = app.files_views[pane];
    using dialog_kind = rv_editor_files_view::rv_editor_files_dialog;

    const bool has_sel = !app.files.selected.empty() && app.files.selected != app.files.root().path;
    const rv_editor_state need_sel = has_sel ? rv_editor_state{} : rv_editor_state{ rv_editor_look::live, "Select a file or folder first" };
    // Square buttons, a coloured letter each until the icons are drawn (editor/docs/icons.md).
    const float side = ImGui::GetFrameHeight();
    if (rv_editor_letter_button("##new_file", 'N', theme.code_green, "New File", theme)) {
        rv_editor_files_ask(view, dialog_kind::new_file, rv_editor_files_target_dir(app), "");
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##new_dir", 'D', theme.code_yellow, "New Folder", theme)) {
        rv_editor_files_ask(view, dialog_kind::new_dir, rv_editor_files_target_dir(app), "");
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##rename", 'R', theme.code_blue, "Rename", theme, need_sel)) {
        rv_editor_files_ask(view, dialog_kind::rename, app.files.selected, app.files.selected.filename().string());
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##delete", 'X', theme.code_red, "Delete", theme, need_sel)) {
        rv_editor_files_ask(view, dialog_kind::remove, app.files.selected, "");
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##refresh", 'U', 0x94e2d5, "Refresh", theme)) {
        app.files.refresh();
    }

    rv_editor_scroll_begin("##tree", ImVec2(0, 0), true);
    rv_editor_files_node(app, view, app.files.root());
    rv_editor_scroll_end(theme);
    rv_editor_files_dialog(app, view, theme);
}

} // namespace rv_editor
