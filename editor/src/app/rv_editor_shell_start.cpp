// The window with no project open (PRJ-07): the Project Catalog. The Toolchest on
// the left; the recent projects, one of them selected, and its card under them.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

#include "imgui.h"

#include "project/rv_editor_templates.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

// A recent project as the catalog shows it.
struct rv_editor_recent_row
{
    std::filesystem::path root;
    bool there = false; // its disc.toml is still there
};

std::vector<rv_editor_recent_row> rv_editor_recent_rows()
{
    std::vector<rv_editor_recent_row> rows;
    for (const std::filesystem::path &root : rv_editor_recent_load()) {
        std::error_code ec;
        rows.push_back({ root, std::filesystem::exists(root / "disc.toml", ec) });
    }
    return rows;
}

// The project's letter, a chip twice the font's height: the stand-in for its icon.
void rv_editor_start_chip(ImVec2 at, const rv_editor_recent_row &row, const rv_editor_theme &theme)
{
    const std::string name = row.root.filename().string();
    const char letter = name.empty() ? '?' : static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    const float side = ImGui::GetFontSize() * 2.0f;
    rv_editor_draw_chip(ImGui::GetWindowDrawList(), at, ImVec2(at.x + side, at.y + side), theme, letter,
        row.there ? theme.selection : theme.text_disabled);
}

void rv_editor_start_toolchest(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header("Toolchest", true, theme);
    if (rv_editor_command_button("##new", 'N', theme.code_green, "New Project...", "A new disc from a starting template",
            theme)) {
        shell.start.form_open = true;
    }
    if (rv_editor_command_button("##open", 'O', theme.selection, "Open Project...", "A game directory with a disc.toml",
            theme)) {
        rv_editor_shell_open_folder(shell);
    }
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    if (rv_editor_command_button("##settings", 'S', theme.code_blue, "Settings...", "The editor's settings", theme)) {
        shell.settings_open = true;
    }
    if (rv_editor_command_button("##help", 'H', theme.code_yellow, "Help", "Keyboard shortcuts", theme)) {
        shell.help_open = true;
    }
}

void rv_editor_start_empty(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
    ImGui::TextUnformatted("No recent projects");
    ImGui::PopStyleColor();
    ImGui::TextWrapped("Open a game project directory or create a minimal Lua or C++ project.");
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight() * 0.5f));
    if (rv_editor_button("New Project...", theme)) {
        shell.start.form_open = true;
    }
    ImGui::SameLine();
    if (rv_editor_button("Open Project...", theme)) {
        rv_editor_shell_open_folder(shell);
    }
}

// The list: one row a project, the chip, its name over its location, whether it
// is there. A click selects; a double click or Enter opens; the context menu forgets.
void rv_editor_start_list(rv_editor_shell &shell, const std::vector<rv_editor_recent_row> &rows, float height,
    const rv_editor_theme &theme)
{
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const float row_h = std::max(ImGui::GetFontSize() * 2.0f, line * 2.0f) + ImGui::GetStyle().CellPadding.y * 2.0f;
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##recent", 3, flags, ImVec2(0.0f, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.0f);
    ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Missing").x * 2.0f);
    ImGui::TableHeadersRow();
    for (const rv_editor_recent_row &row : rows) {
        ImGui::PushID(row.root.c_str());
        ImGui::TableNextRow(ImGuiTableRowFlags_None, row_h);
        ImGui::TableNextColumn();
        const bool selected = row.root == shell.start_selected;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        constexpr ImGuiSelectableFlags select_flags = ImGuiSelectableFlags_SpanAllColumns |
            ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap;
        if (ImGui::Selectable("##row", selected, select_flags, ImVec2(0.0f, row_h))) {
            shell.start_selected = row.root;
        }
        if (row.there && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            rv_editor_shell_request_open(shell, row.root);
        }
        if (ImGui::BeginPopupContextItem("##row_menu")) {
            shell.start_selected = row.root;
            if (rv_editor_menu_item("Open Project", nullptr, row.there ? nullptr : "The project is not there")) {
                rv_editor_shell_request_open(shell, row.root);
            }
            if (ImGui::MenuItem("Remove from Recent")) {
                rv_editor_recent_remove(row.root);
            }
            ImGui::SetItemTooltip("Takes it off this list; the directory stays as it is");
            ImGui::EndPopup();
        }
        rv_editor_start_chip(ImVec2(at.x, at.y + (row_h - ImGui::GetFontSize() * 2.0f) / 2.0f), row, theme);
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
        ImGui::TextUnformatted(row.root.filename().c_str());
        ImGui::PopStyleColor();
        // Dark on the brass of a selected row, dim on the others.
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(selected ? theme.dark : theme.text_disabled));
        ImGui::TextUnformatted(row.root.parent_path().c_str());
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        rv_editor_status(row.there ? "Ready" : "Missing", row.there ? rv_editor_status_kind::ok
                                                                    : rv_editor_status_kind::warning, theme);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// The selected project's card: its whole path, whether its disc.toml is there,
// and Open Project at the bottom right.
void rv_editor_start_card(rv_editor_shell &shell, const rv_editor_recent_row &row, const rv_editor_theme &theme)
{
    rv_editor_pane_header("Selected Project", true, theme);
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
    ImGui::TextUnformatted(row.root.filename().c_str());
    ImGui::PopStyleColor();
    rv_editor_path_row("Path", row.root.string(), theme);
    if (row.there) {
        rv_editor_status("disc.toml found", rv_editor_status_kind::ok, theme);
    } else {
        rv_editor_status("No disc.toml there any more", rv_editor_status_kind::warning, theme);
        ImGui::SameLine();
        if (rv_editor_button("Locate...", theme)) {
            rv_editor_recent_remove(row.root);
            rv_editor_shell_open_folder(shell);
        }
    }
    const char *label = "Open Project";
    const float width = rv_editor_button_width(label);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
    if (rv_editor_button(label, theme, { rv_editor_look::live, row.there ? nullptr : "The project is not there" })) {
        rv_editor_shell_request_open(shell, row.root);
    }
}

void rv_editor_start_catalog(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header("Recent Projects", true, theme);
    const std::vector<rv_editor_recent_row> rows = rv_editor_recent_rows();
    if (rows.empty()) {
        rv_editor_start_empty(shell, theme);
        return;
    }
    // A selection that left the list falls back to the first project.
    const auto kept = std::find_if(rows.begin(), rows.end(),
        [&](const rv_editor_recent_row &r) { return r.root == shell.start_selected; });
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
    const float card = ImGui::GetFrameHeightWithSpacing() * 5.0f + ImGui::GetTextLineHeightWithSpacing();
    rv_editor_start_list(shell, rows, std::max(ImGui::GetContentRegionAvail().y - card, ImGui::GetFrameHeight() * 4.0f),
        theme);
    rv_editor_start_card(shell, selected, theme);
}

} // namespace

void rv_editor_shell_start_screen(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header("Project Catalog", true, theme);
    // Roomier controls here than in the tool panes; the font stays as it is.
    const ImVec2 frame = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame.x * 2.0f, frame.y * 2.0f));
    const float pad = static_cast<float>(theme.pad_px) * theme.scale * 3.0f;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + pad, ImGui::GetCursorPosY() + pad));
    const float column = ImGui::GetFontSize() * 18.0f;
    ImGui::BeginChild("##start_tools", ImVec2(column, -pad), ImGuiChildFlags_Borders);
    rv_editor_start_toolchest(shell, theme);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, pad);
    ImGui::BeginChild("##start_catalog", ImVec2(-pad, -pad), ImGuiChildFlags_Borders);
    rv_editor_start_catalog(shell, theme);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    rv_editor_shell_new_project(shell, theme);
}

} // namespace rv_editor
