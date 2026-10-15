// Files: the project tree and the file operations on it.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <system_error>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_catppuccin_mocha.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Folder glyph colour: the olive theme's selection colour (rv_editor_theme_olive.selection).
constexpr uint32_t folder_color = 0x958831;

// Project manifest file name.
constexpr const char *disc_manifest_filename = "disc.toml";

// String appended to symlink names in file tree.
constexpr const char *symlink_indicator = " ->";

// Input field width for dialogs, in font size units.
constexpr float dialog_field_width_em = 24.0f;

// Divisor for vertical center calculation in file tree rows.
constexpr float vertical_center_divisor = 2.0f;

// Width of space character for indentation alignment in file tree.
constexpr const char *indent_space_probe = " ";

const char *rv_editor_files_dialog_title(rv_editor_files_view::rv_editor_files_dialog dialog)
{
    switch (dialog) {
    case rv_editor_files_view::rv_editor_files_dialog::new_file:
        return rv_editor_text("pane_files.dialog_title_new_file");
    case rv_editor_files_view::rv_editor_files_dialog::new_dir:
        return rv_editor_text("pane_files.dialog_title_new_dir");
    case rv_editor_files_view::rv_editor_files_dialog::rename:
        return rv_editor_text("pane_files.dialog_title_rename");
    case rv_editor_files_view::rv_editor_files_dialog::remove:
        return rv_editor_text("pane_files.dialog_title_remove");
    default:
        return "";
    }
}

struct rv_editor_files_request_row {
    rv_editor_files_view::rv_editor_files_dialog dialog;
    rv_editor_pane_request_kind kind;
};

// The request a dialog's confirm leaves for the app.
constexpr rv_editor_files_request_row files_request_rows[] = {
    { rv_editor_files_view::rv_editor_files_dialog::new_file, rv_editor_pane_request_kind::create_file },
    { rv_editor_files_view::rv_editor_files_dialog::new_dir, rv_editor_pane_request_kind::create_dir },
    { rv_editor_files_view::rv_editor_files_dialog::rename, rv_editor_pane_request_kind::rename },
    { rv_editor_files_view::rv_editor_files_dialog::remove, rv_editor_pane_request_kind::remove },
};

// The row of a dialog's request; nullptr for none and for a dialog without a row.
const rv_editor_files_request_row *rv_editor_files_request_find(rv_editor_files_view::rv_editor_files_dialog dialog)
{
    const auto row = std::ranges::find(files_request_rows, dialog, &rv_editor_files_request_row::dialog);
    return row == std::end(files_request_rows) ? nullptr : row;
}

// The directory a new entry goes into: `sel` when it is a directory,
// its parent when it is a file, the root otherwise.
std::filesystem::path rv_editor_files_target_dir(rv_editor_app &app, const std::filesystem::path &sel)
{
    if (sel.empty()) {
        return app.files.root().path;
    }
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::symlink_status(sel, ec)) ? sel : sel.parent_path();
}

// Asks the shell to select `path` in Files; the selection moves next frame.
void rv_editor_files_select(const rv_editor_ui_port &ui, const std::filesystem::path &path)
{
    ui.requests.push_back({ .kind = rv_editor_ui_request_kind::select_file, .path = path });
}

void rv_editor_files_ask(rv_editor_files_view &view,
    rv_editor_files_view::rv_editor_files_dialog dialog,
    const std::filesystem::path &target,
    const std::string &name)
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
    // At most this many doomed entries are listed by name in the Delete dialog.
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

void rv_editor_files_menu(rv_editor_app &app,
    rv_editor_files_view &view,
    const rv_editor_file_node &node,
    const rv_editor_ui_port &ui)
{
    // The project root is listed, but neither renamed nor deleted; the menu says why.
    const bool is_root = node.path == app.files.root().path;
    const char *rename_why = is_root ? rv_editor_text("pane_files.rename_root_why_not") : nullptr;
    const char *delete_why = is_root ? rv_editor_text("pane_files.delete_root_why_not") : nullptr;
    if (rv_editor_menu_item(rv_editor_text("pane_files.menu_new_file"), nullptr, nullptr)) {
        rv_editor_files_select(ui, node.path);
        const std::filesystem::path target = rv_editor_files_target_dir(app, node.path);
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::new_file, target, "");
    }
    if (rv_editor_menu_item(rv_editor_text("pane_files.menu_new_folder"), nullptr, nullptr)) {
        rv_editor_files_select(ui, node.path);
        const std::filesystem::path target = rv_editor_files_target_dir(app, node.path);
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::new_dir, target, "");
    }
    if (!node.dir && rv_editor_menu_item(rv_editor_text("pane_files.menu_open_as_text"), nullptr, nullptr)) {
        app.open_as_text.insert(node.path);
        app.open_requests.push_back({ node.path, 0 });
    }
    if (rv_editor_menu_item(rv_editor_text("pane_files.menu_rename"), nullptr, rename_why)) {
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::rename, node.path, node.name);
    }
    if (rv_editor_menu_item(rv_editor_text("pane_files.menu_delete"), nullptr, delete_why)) {
        rv_editor_files_ask(view, rv_editor_files_view::rv_editor_files_dialog::remove, node.path, node.name);
    }
}

// The code and colour that stand in for a file's icon.
void rv_editor_files_chip(const rv_editor_file_node &node, const char *&code, uint32_t &color)
{
    if (node.symlink) {
        code = rv_editor_glyph::link;
        color = rv_editor_mocha_overlay0;
        return;
    }
    if (node.dir) {
        code = rv_editor_glyph::folder;
        color = folder_color;
        return;
    }
    rv_editor_file_chip(node.path, code, color);
}

} // namespace

void rv_editor_file_chip(const std::filesystem::path &path, const char *&code, uint32_t &color)
{
    const std::string ext = path.extension().string();
    struct kind {
        const char *ext;
        const char *code;
        uint32_t color;
    };
    // File extension -> list glyph and colour.
    static constexpr kind kinds[] = { { ".lua", rv_editor_glyph::lua, rv_editor_mocha_mauve },
        { ".cpp", rv_editor_glyph::cpp, rv_editor_mocha_blue },
        { ".c", rv_editor_glyph::cpp, rv_editor_mocha_blue },
        { ".cc", rv_editor_glyph::cpp, rv_editor_mocha_blue },
        { ".hpp", rv_editor_glyph::header, rv_editor_mocha_sapphire },
        { ".h", rv_editor_glyph::header, rv_editor_mocha_sapphire },
        { ".toml", rv_editor_glyph::toml, rv_editor_mocha_peach },
        { ".png", rv_editor_glyph::image, rv_editor_mocha_green },
        { ".pcm", rv_editor_glyph::sound, rv_editor_mocha_teal },
        { ".wav", rv_editor_glyph::sound, rv_editor_mocha_teal },
        { ".md", rv_editor_glyph::text, rv_editor_mocha_text },
        { ".txt", rv_editor_glyph::text, rv_editor_mocha_text } };
    code = rv_editor_glyph::other_file;
    color = rv_editor_mocha_subtext0;
    if (path.filename() == disc_manifest_filename) {
        code = rv_editor_glyph::disc_toml;
        color = rv_editor_mocha_peach;
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

void rv_editor_files_node(rv_editor_app &app,
    rv_editor_files_view &view,
    rv_editor_file_node &node,
    const rv_editor_ui_port &ui,
    const rv_editor_theme &theme)
{
    const bool branch = node.dir && !node.symlink;
    rv_editor_tree_style style;
    style.leaf = !branch;
    style.no_push = !branch;
    style.open_on_double_click = branch;
    style.selected = ui.state.files_selected == node.path;
    if (branch) {
        style.open = node.expanded;
    }
    // Leading spaces reserve the widest code's measured width plus the usual item
    // spacing, so every row's name starts at the same place regardless of its code.
    const float space_w = ImGui::CalcTextSize(indent_space_probe).x;
    const float reserve = ImGui::CalcTextSize(rv_editor_glyph::disc_toml).x + ImGui::GetStyle().ItemSpacing.x;
    const int spaces = std::max(1, static_cast<int>(std::ceil(reserve / space_w)));
    const std::string label =
        std::string(static_cast<size_t>(spaces), ' ') + node.name + (node.symlink ? symlink_indicator : "");
    ImGui::PushID(node.path.c_str());
    const rv_editor_tree_result row = rv_editor_tree_row("##node", label.c_str(), theme, style);
    {
        const char *code = rv_editor_glyph::other_file;
        uint32_t color = 0;
        rv_editor_files_chip(node, code, color);
        // A row is one cell high: the stand-in is the code itself, in its colour.
        const float cell = ImGui::GetTextLineHeight();
        const ImVec2 at(ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing(),
            std::floor((ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y - cell) / vertical_center_divisor));
        ImGui::GetWindowDrawList()->AddText(at, rv_editor_col(color), code);
    }
    // A click on the arrow or a double click (toggled) selects too, as the old whole-row click did.
    const bool picked = row.clicked || row.toggled || row.right_clicked;
    if (picked) {
        rv_editor_files_select(ui, node.path);
    }
    if (!branch && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        app.open_requests.push_back({ node.path, 0 });
    }
    if (node.symlink && ImGui::IsItemHovered()) {
        std::error_code ec;
        const std::string link_target = std::filesystem::read_symlink(node.path, ec).generic_string();
        const std::string tooltip_text =
            rv_editor_text_format("pane_files.symlink_tooltip", std::make_format_args(link_target));
        rv_editor_tooltip(tooltip_text.c_str());
    }
    if (rv_editor_context_menu_begin("##menu")) {
        rv_editor_files_menu(app, view, node, ui);
        rv_editor_context_menu_end();
    }
    if (branch) {
        node.expanded = row.open;
        if (row.open) {
            if (!node.listed) {
                app.files.list(node);
            }
            for (rv_editor_file_node &child : node.children) {
                rv_editor_files_node(app, view, child, ui, theme);
            }
            rv_editor_tree_pop();
        }
    }
    ImGui::PopID();
}

void rv_editor_files_dialog(rv_editor_app &app,
    rv_editor_pane_id pane,
    rv_editor_files_view &view,
    const rv_editor_theme &theme)
{
    using dialog_kind = rv_editor_files_view::rv_editor_files_dialog;
    if (view.dialog == dialog_kind::none) {
        return;
    }
    const rv_editor_files_request_row *row = rv_editor_files_request_find(view.dialog);
    if (row == nullptr) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "files dialog without a request closed: impossible call");
        view.dialog = dialog_kind::none;
        return;
    }
    rv_editor_ask_begin(rv_editor_files_dialog_title(view.dialog), theme);

    // While the dialog's request waits or runs, its confirm, name field and Cancel say so;
    // the app closes the dialog on success.
    const char *running_why =
        rv_editor_app_request_pending(app, row->kind) ? rv_editor_text("app_why_not.files_pending") : nullptr;
    const std::string what = view.target.filename().string();
    bool confirm = false;
    if (view.dialog == dialog_kind::remove) {
        std::error_code ec;
        const bool dir = std::filesystem::is_directory(std::filesystem::symlink_status(view.target, ec));
        const std::string dir_suffix = dir ? rv_editor_text("pane_files.remove_with_contents") : "";
        const std::string confirm_msg =
            rv_editor_text_format("pane_files.remove_confirm", std::make_format_args(what, dir_suffix));
        rv_editor_label(confirm_msg.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
        for (const std::string &entry : view.doomed) {
            rv_editor_label(entry.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::bullet);
        }
        if (view.doomed_total > view.doomed.size()) {
            const size_t more_count = view.doomed_total - view.doomed.size();
            const std::string more_text = rv_editor_text_format("pane_files.and_more_items", std::make_format_args(more_count));
            rv_editor_label(more_text.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
        }
    } else {
        const char *rename_label = rv_editor_text("pane_files.new_name");
        const char *name_label = rv_editor_text("pane_files.name");
        const char *label = view.dialog == dialog_kind::rename ? rename_label : name_label;
        const std::string target_path = (view.dialog == dialog_kind::rename ? view.target.parent_path() : view.target).string();
        const std::string header = rv_editor_text_format("pane_files.dialog_header", std::make_format_args(label, target_path));
        rv_editor_label(header.c_str(), rv_editor_label_kind::normal, theme);
        rv_editor_field field;
        field.state = { rv_editor_look::live, running_why };
        field.invalid = view.error.empty() ? nullptr : view.error.c_str();
        if (view.opening) {
            ImGui::SetKeyboardFocusHere();
            view.opening = false;
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * dialog_field_width_em);
        rv_editor_text_field("##name", view.name, sizeof(view.name), theme, field);
        confirm = ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    }
    if (!view.error.empty()) {
        rv_editor_label(view.error.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
    }
    const char *delete_btn = rv_editor_text("pane_files.delete_button");
    const char *ok_btn = rv_editor_text("pane_files.ok_button");
    const char *confirm_button_text = view.dialog == dialog_kind::remove ? delete_btn : ok_btn;
    confirm = rv_editor_button(confirm_button_text, theme, { rv_editor_look::live, running_why }) || confirm;
    ImGui::SameLine();
    const bool cancel =
        rv_editor_button(rv_editor_text("pane_files.cancel_button"), theme, { rv_editor_look::live, running_why });

    if (confirm && running_why == nullptr) {
        // The app runs the request on the next frame and writes the outcome into this view.
        (void)rv_editor_app_request(app, { row->kind, pane, view.target, std::string(view.name) });
    } else if (cancel) {
        view.dialog = dialog_kind::none;
    }
    rv_editor_ask_end();
}

} // namespace

void rv_editor_pane_files(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    if (!app.files.is_open()) {
        rv_editor_label(rv_editor_text("pane_files.no_project_open"),
            rv_editor_label_kind::normal,
            theme,
            rv_editor_label_layout::wrap);
        return;
    }
    rv_editor_files_view &view = app.files_views[pane];
    using dialog_kind = rv_editor_files_view::rv_editor_files_dialog;

    const bool has_sel = !ui.state.files_selected.empty() && ui.state.files_selected != app.files.root().path;
    const char *select_hint = rv_editor_text("pane_files.select_first");
    const rv_editor_state need_sel = has_sel ? rv_editor_state{} : rv_editor_state{ rv_editor_look::live, select_hint };
    // Square buttons, a coloured code each until the icons are drawn.
    const float side = ImGui::GetFrameHeight();
    // A refresh waiting for the next frame blocks Refresh, saying so.
    const bool refresh_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::refresh_files);
    const rv_editor_state refresh_state{ rv_editor_look::live,
        refresh_pending ? rv_editor_text("app_why_not.files_pending") : nullptr };
    rv_editor_shelf_begin("##shelf", theme);
    if (rv_editor_letter_button("##new_file",
            rv_editor_glyph::new_,
            theme.code_green,
            rv_editor_text("pane_files.button_new_file"),
            theme)) {
        rv_editor_files_ask(view, dialog_kind::new_file, rv_editor_files_target_dir(app, ui.state.files_selected), "");
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##new_dir",
            rv_editor_glyph::new_folder,
            theme.code_yellow,
            rv_editor_text("pane_files.button_new_folder"),
            theme)) {
        rv_editor_files_ask(view, dialog_kind::new_dir, rv_editor_files_target_dir(app, ui.state.files_selected), "");
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##rename",
            rv_editor_glyph::rename,
            theme.code_blue,
            rv_editor_text("pane_files.button_rename"),
            theme,
            need_sel)) {
        rv_editor_files_ask(view, dialog_kind::rename, ui.state.files_selected, ui.state.files_selected.filename().string());
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##delete",
            rv_editor_glyph::delete_,
            theme.code_red,
            rv_editor_text("pane_files.button_delete"),
            theme,
            need_sel)) {
        rv_editor_files_ask(view, dialog_kind::remove, ui.state.files_selected, "");
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##refresh",
            rv_editor_glyph::refresh,
            rv_editor_mocha_teal,
            rv_editor_text("pane_files.button_refresh"),
            theme,
            refresh_state)) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::refresh_files, pane });
    }
    rv_editor_shelf_end();

    rv_editor_files_dialog(app, pane, view, theme);
    rv_editor_well_begin("##well", ImVec2(0, 0), theme, ImGuiWindowFlags_HorizontalScrollbar);
    rv_editor_files_node(app, view, app.files.root(), ui, theme);
    rv_editor_well_end();
}

} // namespace rv_editor
