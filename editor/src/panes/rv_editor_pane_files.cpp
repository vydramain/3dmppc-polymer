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

// The icon of a file's row (spec section 16): its kind by extension, never a guess.
const char *rv_editor_files_icon(const rv_editor_file_node &node)
{
    if (node.symlink) {
        return "symlink";
    }
    if (node.dir) {
        return node.expanded ? "folder-open" : "folder";
    }
    if (node.name == "disc.toml") {
        return "file-disc";
    }
    const std::string ext = node.path.extension().string();
    struct kind
    {
        const char *ext;
        const char *icon;
    };
    static constexpr kind kinds[] = { { ".lua", "file-lua" }, { ".cpp", "file-cpp" }, { ".c", "file-cpp" },
        { ".cc", "file-cpp" }, { ".hpp", "file-header" }, { ".h", "file-header" }, { ".toml", "file-toml" },
        { ".png", "file-image" }, { ".pcm", "file-sound" }, { ".wav", "file-sound" }, { ".md", "file-doc" },
        { ".txt", "file-doc" } };
    for (const kind &k : kinds) {
        if (ext == k.ext) {
            return k.icon;
        }
    }
    return "file-unknown";
}

void rv_editor_files_node(rv_editor_app &app, rv_editor_files_view &view, rv_editor_file_node &node,
    const rv_editor_theme &theme)
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
    ImGui::PushID(node.path.c_str());
    if (branch) {
        ImGui::SetNextItemOpen(node.expanded);
    }
    // A row is at least 20 px (VIS-05); the icon and the name are drawn over it.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 3.0f * theme.scale));
    const bool open = ImGui::TreeNodeEx("##node", flags | ImGuiTreeNodeFlags_FramePadding, "%s", "");
    ImGui::PopStyleVar();
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float art = std::floor(16.0f * theme.scale);
        const ImVec2 row_min = ImGui::GetItemRectMin();
        const ImVec2 row_max = ImGui::GetItemRectMax();
        const float x = row_min.x + ImGui::GetTreeNodeToLabelSpacing();
        rv_editor_icon_draw(dl, rv_editor_files_icon(node), ImVec2(x, std::floor((row_min.y + row_max.y - art) / 2.0f)),
            theme);
        const std::string label = node.name + (node.symlink ? " ->" : "");
        const bool selected = app.files.selected == node.path;
        dl->AddText(ImVec2(x + art + 4.0f * theme.scale, std::floor((row_min.y + row_max.y - ImGui::GetFontSize()) / 2.0f)),
            rv_editor_col(selected ? theme.text_on_selection : theme.text), label.c_str());
        if (ImGui::IsItemHovered() && !node.symlink) {
            ImGui::SetItemTooltip("%s", node.path.c_str());
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        app.files.selected = node.path;
    }
    if (!branch && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        app.open_requests.push_back({ node.path, 0 });
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
                rv_editor_files_node(app, view, child, theme);
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
                app.open_requests.push_back({ view.target / name, 0 });
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
    // Navigation and creation first; Delete last, set apart and outlined in red.
    const float side = ImGui::GetFrameHeight();
    if (rv_editor_image_button("##new_file", "new-file", "New File", theme)) {
        rv_editor_files_ask(view, dialog_kind::new_file, rv_editor_files_target_dir(app), "");
    }
    rv_editor_flow(side);
    if (rv_editor_image_button("##new_dir", "new-folder", "New Folder", theme)) {
        rv_editor_files_ask(view, dialog_kind::new_dir, rv_editor_files_target_dir(app), "");
    }
    rv_editor_flow(side);
    if (rv_editor_image_button("##rename", "rename", "Rename", theme, need_sel)) {
        rv_editor_files_ask(view, dialog_kind::rename, app.files.selected, app.files.selected.filename().string());
    }
    rv_editor_flow(side);
    if (rv_editor_image_button("##refresh", "refresh", "Refresh", theme)) {
        app.files.refresh();
    }
    // A gap sets Delete apart from the rest.
    rv_editor_flow(side * 1.5f);
    ImGui::Dummy(ImVec2(side * 0.5f, side));
    ImGui::SameLine(0.0f, 0.0f);
    if (rv_editor_image_button("##delete", "delete", "Delete (asks first)", theme, need_sel, true)) {
        rv_editor_files_ask(view, dialog_kind::remove, app.files.selected, "");
    }

    rv_editor_scroll_begin("##tree", ImVec2(0, 0), true);
    rv_editor_files_node(app, view, app.files.root(), theme);
    rv_editor_scroll_end(theme);
    rv_editor_files_dialog(app, view, theme);
}

} // namespace rv_editor
