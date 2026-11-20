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
#include "panes/rv_editor_pane_files.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_catppuccin_mocha.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_mode.hpp"
#include "ui/rv_editor_widgets_table.hpp"

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

// Divisor for vertical center calculation in file tree rows.
constexpr float vertical_center_divisor = 2.0f;

// Width of space character for indentation alignment in file tree.
constexpr const char *indent_space_probe = " ";

// Asks the shell to select `path` in Files; the selection moves next frame.
void rv_editor_files_select(const rv_editor_ui_port &ui, const std::filesystem::path &path)
{
    ui.requests.push_back({ .kind = rv_editor_ui_request_kind::select_file, .path = path });
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
        // The folder colour is the highlight's own, so on the picked row the glyph is drawn in the bright text colour.
        if (node.dir && !node.symlink && style.selected) {
            color = theme.text_bright;
        }
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
        // A plain file with a tab view opens in the Scene tile; a directory (a link to one
        // too) and the rest go to the code editor.
        if (node.dir || !rv_editor_scene_tab_open(app, ui, node.path)) {
            app.open_requests.push_back({ node.path, 0 });
        }
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

} // namespace

rv_editor_size rv_editor_files_minimum()
{
    // The tallest strip is the question with its name field in a row under the text.
    rv_editor_size size = rv_editor_tiled_minimum(rv_editor_files_content_width(),
        rv_editor_tiled_shelf_block() + rv_editor_tiled_strip_block(true));
    // The card of a folder's delete replaces the content: the tile holds it whole too.
    const ImVec2 card = rv_editor_files_card_min_size();
    const ImGuiStyle &style = ImGui::GetStyle();
    size.w = std::max(size.w, static_cast<int32_t>(std::ceil(card.x + style.WindowPadding.x)));
    size.h = std::max(size.h, static_cast<int32_t>(std::ceil(card.y + style.WindowPadding.y)));
    return size;
}

void rv_editor_pane_files(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    rv_editor_files_view &view = app.files_views[pane];
    if (rv_editor_files_card(app, pane, view, theme)) {
        return;
    }
    rv_editor_files_shelf(app, pane, view, ui, theme);
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, rv_editor_files_content_width());
    (void)rv_editor_tiled_area_begin("##files_area",
        rv_editor_files_content_width(),
        theme,
        {},
        false,
        0.0f,
        rv_editor_files_strip_height(app, view, strip_w));
    if (app.files.is_open()) {
        rv_editor_files_node(app, view, app.files.root(), ui, theme);
    } else {
        rv_editor_content_empty_row(rv_editor_text("pane_files.no_project_open"), theme);
    }
    rv_editor_tiled_area_end(theme);
    rv_editor_files_strip(app, pane, view, strip_w, theme);
}

} // namespace rv_editor
