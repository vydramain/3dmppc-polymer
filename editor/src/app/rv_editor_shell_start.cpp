// The window with no project open: the Project Catalog. The Toolchest on
// the left, and beside it the page it chose: the recent projects with the selected
// one's card, New Project or Open Project. No page covers another.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

#include "imgui.h"

#include "project/rv_editor_templates.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_table.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

// Manifest filename identifying a project directory.
constexpr const char *disc_manifest_filename = "disc.toml";

// Placeholder character when the project directory name is empty.
constexpr char empty_name_placeholder = '?';

// Chip is a square twice the font height; used for project icon columns and rows.
constexpr float chip_height_em = 2.0f;

// Recent projects section spacing when there are no projects.
constexpr float empty_recent_spacing_factor = 0.5f;

// A catalog row is at least this many font heights or text lines tall.
constexpr float row_height_min_factor = 2.0f;

// Cell padding counts twice in a row's height: above and below the text.
constexpr float cell_padding_sides = 2.0f;

// Number of columns in the recent projects catalog table.
constexpr int table_num_columns = 3;

// Frame padding is scaled by this factor for roomier controls on the start screen.
constexpr float frame_padding_scale = 2.0f;

// Start screen padding is scaled by this factor for spacing.
constexpr float pad_scale_factor = 3.0f;

// Toolchest (left panel with command buttons) width in font size units.
constexpr float toolchest_width_em = 18.0f;

// Vertical centering divisor: divide available height by 2 to center an element.
constexpr float center_divisor = 2.0f;

// A recent project as the catalog shows it.
struct rv_editor_recent_row {
    std::filesystem::path root;
    bool there = false; // its disc.toml is still there
};

std::vector<rv_editor_recent_row> rv_editor_recent_rows()
{
    std::vector<rv_editor_recent_row> rows;
    for (const std::filesystem::path &root : rv_editor_recent_load()) {
        std::error_code ec;
        rows.push_back({ root, std::filesystem::exists(root / disc_manifest_filename, ec) });
    }
    return rows;
}

// The row's display name: its directory name, or the whole path if that is empty.
std::string rv_editor_start_name(const rv_editor_recent_row &row)
{
    const std::string name = row.root.filename().string();
    return name.empty() ? row.root.string() : name;
}

// The project's letter, a chip twice the font's height: the stand-in for its icon.
void rv_editor_start_chip(ImVec2 at, const rv_editor_recent_row &row, const rv_editor_theme &theme)
{
    const std::string name = row.root.filename().string();
    const char letter =
        name.empty() ? empty_name_placeholder : static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    const float side = ImGui::GetFontSize() * chip_height_em;
    rv_editor_draw_chip(ImGui::GetWindowDrawList(),
        at,
        ImVec2(at.x + side, at.y + side),
        theme,
        letter,
        row.there ? theme.selection : theme.text_disabled);
}

// The Toolchest row of the page in front looks pressed.
rv_editor_state rv_editor_start_row(const rv_editor_shell &shell, rv_editor_start_page page)
{
    return { shell.start_page == page ? rv_editor_look::pressed : rv_editor_look::live, nullptr };
}

void rv_editor_start_toolchest(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header(rv_editor_text("shell_start.toolchest_title"), true, theme);
    if (rv_editor_command_button("##recent",
            rv_editor_glyph::recent,
            theme.code_cyan,
            rv_editor_text("shell_start.recent_projects"),
            rv_editor_text("shell_start.recent_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::recent))) {
        shell.start_page = rv_editor_start_page::recent;
    }
    if (rv_editor_command_button("##new",
            rv_editor_glyph::new_,
            theme.code_green,
            rv_editor_text("shell_start.new_project"),
            rv_editor_text("shell_start.new_project_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::new_project))) {
        shell.start_page = rv_editor_start_page::new_project;
    }
    if (rv_editor_command_button("##open",
            rv_editor_glyph::open,
            theme.selection,
            rv_editor_text("shell_start.open_project"),
            rv_editor_text("shell_start.open_project_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::open_project))) {
        rv_editor_shell_open_project(shell);
    }
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    if (rv_editor_command_button("##settings",
            rv_editor_glyph::settings,
            theme.code_blue,
            rv_editor_text("shell_start.settings"),
            rv_editor_text("shell_start.settings_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::settings))) {
        rv_editor_shell_page(shell, rv_editor_start_page::settings);
    }
    if (rv_editor_command_button("##help",
            rv_editor_glyph::help,
            theme.code_yellow,
            rv_editor_text("shell_start.help"),
            rv_editor_text("shell_start.help_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::help))) {
        rv_editor_shell_page(shell, rv_editor_start_page::help);
    }
    if (rv_editor_command_button("##manual",
            rv_editor_glyph::manual,
            theme.code_yellow,
            rv_editor_text("manual.start_label"),
            rv_editor_text("manual.start_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::manual))) {
        rv_editor_shell_page(shell, rv_editor_start_page::manual);
    }
}

void rv_editor_start_empty(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_label(rv_editor_text("shell_start.no_recent"), rv_editor_label_kind::bright, theme);
    rv_editor_label(rv_editor_text("shell_start.no_recent_hint"),
        rv_editor_label_kind::normal,
        theme,
        rv_editor_label_layout::wrap);
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight() * empty_recent_spacing_factor));
    if (rv_editor_button(rv_editor_text("shell_start.new_project"), theme)) {
        shell.start_page = rv_editor_start_page::new_project;
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("shell_start.open_project"), theme)) {
        rv_editor_shell_open_project(shell);
    }
}

// The list: one row a project, the chip, its name over its location, whether it
// is there. A click selects; a double click or Enter opens; the context menu forgets.
// The table does not scroll; the well around it does.
void rv_editor_start_list(rv_editor_shell &shell, const std::vector<rv_editor_recent_row> &rows, const rv_editor_theme &theme)
{
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const float row_h = std::max(ImGui::GetFontSize() * row_height_min_factor, line * row_height_min_factor) +
        ImGui::GetStyle().CellPadding.y * cell_padding_sides;
    const float chip_side = ImGui::GetFontSize() * chip_height_em;
    const rv_editor_table_column columns[table_num_columns] = {
        { "", rv_editor_table_width::fixed },
        { rv_editor_text("shell_start.table_project"), rv_editor_table_width::stretch },
        { rv_editor_text("shell_start.table_status"), rv_editor_table_width::fixed },
    };
    const rv_editor_table_style style = { .borders = true, .row_bands = true, .header = true };
    if (!rv_editor_table_begin("##recent", columns, table_num_columns, style)) {
        return;
    }
    for (const rv_editor_recent_row &row : rows) {
        ImGui::PushID(row.root.c_str());
        rv_editor_table_row();
        rv_editor_table_cell();
        const bool selected = row.root == shell.start_selected;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        // The chip is drawn over the row's start: reserve its width for the fixed column,
        // then put the cursor back so the row is placed at the cell start.
        ImGui::Dummy(ImVec2(chip_side, row_h));
        ImGui::SetCursorScreenPos(at);
        const rv_editor_list_result pick = rv_editor_list_row("##row",
            "",
            theme,
            { .selected = selected, .span_columns = true, .overlap = true, .height = row_h });
        if (pick.clicked) {
            shell.start_selected = row.root;
        }
        if (pick.double_clicked && row.there) {
            rv_editor_shell_request_open(shell, row.root);
        }
        if (rv_editor_context_menu_begin("##row_menu")) {
            shell.start_selected = row.root;
            const std::string name = rv_editor_start_name(row);
            const std::string open_label = rv_editor_text_format("shell_start.open_dynamic", std::make_format_args(name));
            if (rv_editor_menu_item(open_label.c_str(),
                    nullptr,
                    row.there ? nullptr : rv_editor_text("shell_start.not_there"))) {
                rv_editor_shell_request_open(shell, row.root);
            }
            if (rv_editor_menu_item(rv_editor_text("shell_start.remove_recent"), nullptr, nullptr)) {
                shell.recent_removed_at = rv_editor_recent_remove(row.root);
                shell.recent_removed = row.root;
            }
            rv_editor_item_tooltip(rv_editor_text("shell_start.remove_recent_tooltip"));
            rv_editor_context_menu_end();
        }
        rv_editor_start_chip(ImVec2(at.x, at.y + (row_h - chip_side) / center_divisor), row, theme);
        rv_editor_table_cell();
        rv_editor_label(row.root.filename().c_str(), rv_editor_label_kind::bright, theme);
        // Dark on the brass of a selected row, dim on the others.
        const rv_editor_label_kind parent_kind = selected ? rv_editor_label_kind::on_selection : rv_editor_label_kind::dim;
        rv_editor_label(row.root.parent_path().c_str(), parent_kind, theme);
        rv_editor_table_cell();
        const char *available = rv_editor_text("shell_start.status_available");
        const char *missing = rv_editor_text("shell_start.status_missing");
        const char *status = row.there ? available : missing;
        rv_editor_status_kind kind = row.there ? rv_editor_status_kind::ok : rv_editor_status_kind::warning;
        rv_editor_status(status, kind, theme);
        ImGui::PopID();
    }
    rv_editor_table_end();
}

// The selected project's card: its whole path, whether its disc.toml is there,
// and Open Project at the bottom right.
void rv_editor_start_card(rv_editor_shell &shell, const rv_editor_recent_row &row, const rv_editor_theme &theme)
{
    rv_editor_pane_header(rv_editor_text("shell_start.selected_project"), true, theme);
    rv_editor_label(row.root.filename().c_str(), rv_editor_label_kind::bright, theme);
    rv_editor_path_row(rv_editor_text("shell_start.path_label"), row.root.string(), theme);
    if (row.there) {
        rv_editor_status(rv_editor_text("shell_start.disc_found"), rv_editor_status_kind::ok, theme);
    } else {
        rv_editor_status(rv_editor_text("shell_start.disc_missing"), rv_editor_status_kind::warning, theme);
        ImGui::SameLine();
        if (rv_editor_button(rv_editor_text("shell_start.locate"), theme)) {
            rv_editor_recent_remove(row.root);
            const std::string filename_str = row.root.filename().string();
            const std::string locate_purpose =
                rv_editor_text_format("shell_start.locate_dynamic", std::make_format_args(filename_str));
            rv_editor_browser_start(shell.open_browser,
                locate_purpose,
                rv_editor_text("shell_start.open_project"),
                rv_editor_browse_pick::directory,
                row.root.parent_path());
            rv_editor_shell_open_project(shell);
        }
    }
    const std::string name_str = rv_editor_start_name(row);
    const std::string label = rv_editor_text_format("shell_start.open_dynamic", std::make_format_args(name_str));
    const float width = rv_editor_button_width(label.c_str());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
    if (rv_editor_button(label.c_str(),
            theme,
            { rv_editor_look::live, row.there ? nullptr : rv_editor_text("shell_start.not_there") })) {
        rv_editor_shell_request_open(shell, row.root);
    }
}

void rv_editor_start_catalog(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header(rv_editor_text("shell_start.recent_projects"), true, theme);
    // What Remove from Recent took, with the way back; the directory was never touched.
    if (!shell.recent_removed.empty()) {
        const std::string removed_filename = shell.recent_removed.filename().string();
        const std::string removed = rv_editor_text_format("shell_start.removed_text", std::make_format_args(removed_filename));
        ImGui::AlignTextToFramePadding();
        rv_editor_label(removed.c_str(), rv_editor_label_kind::normal, theme);
        ImGui::SameLine();
        if (rv_editor_button(rv_editor_text("shell_start.undo"), theme)) {
            rv_editor_recent_restore(shell.recent_removed, shell.recent_removed_at);
            shell.start_selected = shell.recent_removed;
            shell.recent_removed.clear();
        }
    }
    const std::vector<rv_editor_recent_row> rows = rv_editor_recent_rows();
    if (rows.empty()) {
        rv_editor_start_empty(shell, theme);
        return;
    }
    // A selection that left the list falls back to the first project.
    const auto kept = std::find_if(rows.begin(), rows.end(), [&](const rv_editor_recent_row &r) {
        return r.root == shell.start_selected;
    });
    size_t index = kept != rows.end() ? static_cast<size_t>(kept - rows.begin()) : 0;
    // While the catalog has the keyboard the arrows move the selection and Enter opens it.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && index + 1 < rows.size()) {
            ++index;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && index > 0) {
            --index;
        }
        const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        if (enter && rows[index].there) {
            rv_editor_shell_request_open(shell, rows[index].root);
        }
    }
    const rv_editor_recent_row &selected = rows[index];
    shell.start_selected = selected.root;
    // Full page width, like the header bars; only as tall as its rows.
    ImGui::BeginChild("##start_recent_col", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY);
    rv_editor_start_list(shell, rows, theme);
    rv_editor_start_card(shell, selected, theme);
    ImGui::EndChild();
}

} // namespace

void rv_editor_page_open_project(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_browser &b = shell.open_browser;
    if (b.purpose.empty()) {
        const std::filesystem::path from =
            shell.app.project.open ? shell.app.project.root.parent_path() : shell.start_selected.parent_path();
        rv_editor_browser_start(b,
            rv_editor_text("shell_start.choose_game_dir"),
            rv_editor_text("shell_start.open_project"),
            rv_editor_browse_pick::directory,
            from);
    }
    rv_editor_pane_header(rv_editor_text("shell_start.open_project"), true, theme);
    std::error_code ec;
    const std::filesystem::path target = rv_editor_browser_target(b);
    const bool found = std::filesystem::exists(target / disc_manifest_filename, ec);
    const std::string target_path = target.string();
    const std::string found_msg = rv_editor_text_format("shell_start.disc_found_with_path", std::make_format_args(target_path));
    const std::string notfound_msg = rv_editor_text_format("shell_start.no_disc_in_path", std::make_format_args(target_path));
    const std::string &about = found ? found_msg : notfound_msg;
    rv_editor_status(about.c_str(), found ? rv_editor_status_kind::ok : rv_editor_status_kind::warning, theme);
    std::filesystem::path picked;
    const rv_editor_browse_result r =
        rv_editor_browser_draw(b, 0.0f, found ? nullptr : rv_editor_text("shell_start.no_disc_in_this_dir"), picked, theme);
    if (r == rv_editor_browse_result::none) {
        return;
    }
    // Picked or cancelled, the next Open Project starts afresh; without a project
    // the window goes back to the recent projects.
    b = {};
    if (!shell.app.project.open) {
        shell.start_page = rv_editor_start_page::recent;
    }
    if (r == rv_editor_browse_result::picked) {
        rv_editor_shell_request_open(shell, picked);
    }
}

void rv_editor_shell_start_screen(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header(rv_editor_text("shell_start.catalog_title"), true, theme);
    // Roomier controls here than in the tool panes; the font stays as it is.
    const ImVec2 frame = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame.x * frame_padding_scale, frame.y * frame_padding_scale));
    const float pad = static_cast<float>(theme.pad_px) * theme.scale * pad_scale_factor;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + pad, ImGui::GetCursorPosY() + pad));
    const float column = ImGui::GetFontSize() * toolchest_width_em;
    rv_editor_well_begin("##start_tools", ImVec2(column, -pad), theme);
    rv_editor_start_toolchest(shell, theme);
    rv_editor_well_end();
    ImGui::SameLine(0.0f, pad);
    rv_editor_well_begin("##start_catalog", ImVec2(-pad, -pad), theme);
    switch (shell.start_page) {
    case rv_editor_start_page::recent:
        rv_editor_start_catalog(shell, theme);
        break;
    case rv_editor_start_page::new_project:
        rv_editor_page_new_project(shell, theme);
        break;
    case rv_editor_start_page::open_project:
        rv_editor_page_open_project(shell, theme);
        break;
    case rv_editor_start_page::settings:
        rv_editor_page_settings(shell, theme);
        break;
    case rv_editor_start_page::help:
        rv_editor_page_help(shell, theme);
        break;
    case rv_editor_start_page::manual:
        rv_editor_page_manual(shell, theme);
        break;
    }
    rv_editor_well_end();
    ImGui::PopStyleVar();
}

} // namespace rv_editor
