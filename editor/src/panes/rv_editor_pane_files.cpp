// Files: the project tree and the file operations on it.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <system_error>

#include "imgui.h"

#include "panes/rv_editor_panes.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
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
    view.doomed.clear();
    view.doomed_total = 0;
    if (dialog != rv_editor_files_view::rv_editor_files_dialog::remove) {
        return;
    }
    // Listed once, when asked: the first entries by name and how many there are.
    constexpr size_t shown = 12;
    const std::filesystem::path base = target.parent_path();
    const auto note = [&](const std::filesystem::path &p) {
        if (view.doomed.size() < shown) {
            std::error_code ec;
            view.doomed.push_back(std::filesystem::relative(p, base, ec).generic_string());
        }
        ++view.doomed_total;
    };
    note(target);
    std::error_code ec;
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(target, ec))) {
        return;
    }
    for (std::filesystem::recursive_directory_iterator it(target, ec), end; !ec && it != end; it.increment(ec)) {
        note(it->path());
    }
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
    if (!node.dir && ImGui::MenuItem("Open as Text")) {
        app.open_as_text.insert(node.path);
        app.open_requests.push_back({ node.path, 0 });
    }
    const bool is_root = node.path == app.files.root().path;
    if (ImGui::MenuItem("Rename", nullptr, false, !is_root)) {
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::rename, node.path, node.name);
    }
    if (ImGui::MenuItem("Delete", nullptr, false, !is_root)) {
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::remove, node.path, node.name);
    }
}

// The code and colour that stand in for a file's icon.
void rv_editor_files_chip(const rv_editor_file_node &node, const char *&code, uint32_t &color)
{
    if (node.symlink) {
        code = rv_editor_glyph::link;
        color = 0x77836b;
        return;
    }
    if (node.dir) {
        code = rv_editor_glyph::folder;
        color = 0x958831;
        return;
    }
    rv_editor_file_chip(node.path, code, color);
}

} // namespace

void rv_editor_file_chip(const std::filesystem::path &path, const char *&code, uint32_t &color)
{
    const std::string ext = path.extension().string();
    struct kind
    {
        const char *ext;
        const char *code;
        uint32_t color;
    };
    static constexpr kind kinds[] = { { ".lua", rv_editor_glyph::lua, 0xb98bb4 },
        { ".cpp", rv_editor_glyph::cpp, 0x7da3c4 }, { ".c", rv_editor_glyph::cpp, 0x7da3c4 },
        { ".cc", rv_editor_glyph::cpp, 0x7da3c4 }, { ".hpp", rv_editor_glyph::header, 0x6fb0bf },
        { ".h", rv_editor_glyph::header, 0x6fb0bf }, { ".toml", rv_editor_glyph::toml, 0xd99a5e },
        { ".png", rv_editor_glyph::image, 0xa3bf6e }, { ".pcm", rv_editor_glyph::sound, 0x79b8a4 },
        { ".wav", rv_editor_glyph::sound, 0x79b8a4 }, { ".md", rv_editor_glyph::text, 0xd8ded3 },
        { ".txt", rv_editor_glyph::text, 0xd8ded3 } };
    code = rv_editor_glyph::other_file;
    color = 0xa3ac97;
    if (path.filename() == "disc.toml") {
        code = rv_editor_glyph::disc_toml;
        color = 0xd99a5e;
        return;
    }
    for (const kind &k : kinds) {
        if (ext == k.ext) {
            code = k.code;
            color = k.color;
            return;
        }
    }
}

namespace
{

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
    // Leading spaces reserve the widest code's measured width plus the usual item
    // spacing, so every row's name starts at the same place regardless of its code.
    const float space_w = ImGui::CalcTextSize(" ").x;
    const float reserve = ImGui::CalcTextSize(rv_editor_glyph::disc_toml).x + ImGui::GetStyle().ItemSpacing.x;
    const int spaces = std::max(1, static_cast<int>(std::ceil(reserve / space_w)));
    const std::string label = std::string(static_cast<size_t>(spaces), ' ') + node.name + (node.symlink ? " ->" : "");
    ImGui::PushID(node.path.c_str());
    if (branch) {
        ImGui::SetNextItemOpen(node.expanded);
    }
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
    {
        const char *code = rv_editor_glyph::other_file;
        uint32_t color = 0;
        rv_editor_files_chip(node, code, color);
        // A row is one cell high: the stand-in is the code itself, in its colour.
        const float cell = ImGui::GetTextLineHeight();
        const ImVec2 at(ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing(),
            std::floor((ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y - cell) / 2.0f));
        ImGui::GetWindowDrawList()->AddText(at, rv_editor_col(color), code);
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
    rv_editor_ask_begin(rv_editor_files_dialog_title(view.dialog), theme);

    const std::string what = view.target.filename().string();
    bool confirm = false;
    if (view.dialog == dialog_kind::remove) {
        std::error_code ec;
        const bool dir = std::filesystem::is_directory(std::filesystem::symlink_status(view.target, ec));
        ImGui::TextWrapped("Delete %s%s? This cannot be undone. It removes:", what.c_str(),
            dir ? " and everything in it" : "");
        for (const std::string &entry : view.doomed) {
            ImGui::BulletText("%s", entry.c_str());
        }
        if (view.doomed_total > view.doomed.size()) {
            ImGui::Text("and %zu more", view.doomed_total - view.doomed.size());
        }
    } else {
        ImGui::Text("%s in %s", view.dialog == dialog_kind::rename ? "New name" : "Name",
            (view.dialog == dialog_kind::rename ? view.target.parent_path() : view.target).c_str());
        rv_editor_field field;
        field.invalid = view.error.empty() ? nullptr : view.error.c_str();
        if (view.opening) {
            ImGui::SetKeyboardFocusHere();
            view.opening = false;
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
    const bool cancel = rv_editor_button("Cancel", theme);

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
        }
    } else if (cancel) {
        view.dialog = dialog_kind::none;
    }
    rv_editor_ask_end();
}

} // namespace

void rv_editor_pane_files(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    if (!app.files.is_open()) {
        ImGui::TextWrapped("No project is open: File > Open Project...");
        return;
    }
    rv_editor_files_view &view = app.files_views[pane];
    using dialog_kind = rv_editor_files_view::rv_editor_files_dialog;

    const bool has_sel = !app.files.selected.empty() && app.files.selected != app.files.root().path;
    const rv_editor_state need_sel = has_sel ? rv_editor_state{} : rv_editor_state{ rv_editor_look::live, "Select a file or folder first" };
    // Square buttons, a coloured code each until the icons are drawn.
    const float side = ImGui::GetFrameHeight();
    rv_editor_shelf_begin("##shelf", theme);
    if (rv_editor_letter_button("##new_file", rv_editor_glyph::new_, theme.code_green, "New File", theme)) {
        rv_editor_files_ask(view, dialog_kind::new_file, rv_editor_files_target_dir(app), "");
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##new_dir", rv_editor_glyph::new_folder, theme.code_yellow, "New Folder", theme)) {
        rv_editor_files_ask(view, dialog_kind::new_dir, rv_editor_files_target_dir(app), "");
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##rename", rv_editor_glyph::rename, theme.code_blue, "Rename", theme, need_sel)) {
        rv_editor_files_ask(view, dialog_kind::rename, app.files.selected, app.files.selected.filename().string());
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##delete", rv_editor_glyph::delete_, theme.code_red, "Delete", theme, need_sel)) {
        rv_editor_files_ask(view, dialog_kind::remove, app.files.selected, "");
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##refresh", rv_editor_glyph::refresh, 0x79b8a4, "Refresh", theme)) {
        app.files.refresh();
    }
    rv_editor_shelf_end();

    rv_editor_files_dialog(app, view, theme);
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    rv_editor_scroll_begin("##tree", ImVec2(0, 0), true);
    rv_editor_files_node(app, view, app.files.root());
    rv_editor_scroll_end(theme);
    rv_editor_well_end();
}

} // namespace rv_editor
