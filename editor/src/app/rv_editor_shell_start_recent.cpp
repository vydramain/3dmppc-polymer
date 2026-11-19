// The Recent Projects page of the start screen: the projects opened lately as records with a status in a content area,
// and under it the selected project's card, a fixed two rows, so nothing moves when the selection or the list changes.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_pane_tiled.hpp"
#include "project/rv_editor_templates.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"

namespace rv_editor
{

namespace
{

// Manifest filename identifying a project directory.
constexpr const char *disc_manifest_filename = "disc.toml";

// The content area is at least this wide, in font sizes.
constexpr float area_min_em = 24.0f;

// A recent project as the page shows it.
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
std::string rv_editor_recent_name(const rv_editor_recent_row &row)
{
    const std::string name = row.root.filename().string();
    return name.empty() ? row.root.string() : name;
}

// The card under the records: the gap, the status line, the gap and the button row.
float rv_editor_recent_card_height()
{
    return 2.0f * ImGui::GetStyle().ItemSpacing.y + 2.0f * ImGui::GetFrameHeight();
}

float rv_editor_recent_buttons_width()
{
    const char *labels[] = { rv_editor_text("shell_start.open_project"),
        rv_editor_text("shell_start.locate"),
        rv_editor_text("shell_start.undo") };
    float width = 2.0f * ImGui::GetStyle().ItemSpacing.x;
    for (const char *label : labels) {
        width += rv_editor_button_width(label);
    }
    return width;
}

// The content area shows this many whole records at the minimum, with the bar under them.
constexpr float area_min_records = 4.0f;

float rv_editor_recent_area_floor()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float record = 2.0f * ImGui::GetTextLineHeight() + 2.0f * style.ItemSpacing.y;
    return area_min_records * record + style.ItemSpacing.y + std::floor(style.ScrollbarSize) + 2.0f * style.FramePadding.y;
}

// The widest row of the page: the buttons, the longer status, or the area's own floor.
float rv_editor_recent_content_width()
{
    const rv_editor_theme theme = rv_editor_theme_olive; // colours do not matter for a width
    const float found = rv_editor_status_width(rv_editor_text("shell_start.disc_found"), rv_editor_status_kind::ok, theme);
    const float lost =
        rv_editor_status_width(rv_editor_text("shell_start.disc_missing"), rv_editor_status_kind::warning, theme);
    return std::max({ ImGui::GetFontSize() * area_min_em, rv_editor_recent_buttons_width(), found, lost });
}

// What a right click on a record offers.
void rv_editor_recent_menu(rv_editor_shell &shell, const rv_editor_recent_row &row)
{
    if (!rv_editor_popup_begin("##row_menu")) {
        return;
    }
    const std::string name = rv_editor_recent_name(row);
    const std::string open_label = rv_editor_text_format("shell_start.open_dynamic", std::make_format_args(name));
    if (rv_editor_menu_item(open_label.c_str(), nullptr, row.there ? nullptr : rv_editor_text("shell_start.not_there"))) {
        rv_editor_shell_request_open(shell, row.root);
    }
    if (rv_editor_menu_item(rv_editor_text("shell_start.remove_recent"), nullptr, nullptr)) {
        shell.recent_removed_at = rv_editor_recent_remove(row.root);
        shell.recent_removed = row.root;
    }
    rv_editor_item_tooltip(rv_editor_text("shell_start.remove_recent_tooltip"));
    rv_editor_popup_end();
}

// One record per project: a click selects, a double click or Enter opens, the context menu forgets.
void rv_editor_recent_records(rv_editor_shell &shell,
    const std::vector<rv_editor_recent_row> &rows,
    const rv_editor_theme &theme)
{
    if (rows.empty()) {
        rv_editor_content_empty_row(rv_editor_text("shell_start.no_recent"), theme);
        return;
    }
    const char *words[] = { rv_editor_text("shell_start.status_available"), rv_editor_text("shell_start.status_missing") };
    rv_editor_record_style style;
    style.status_width = rv_editor_record_status_width(words, 2);
    for (const rv_editor_recent_row &row : rows) {
        ImGui::PushID(row.root.c_str());
        style.selected = row.root == shell.start_selected;
        const std::string name = row.root.filename().string();
        const std::string parent = row.root.parent_path().string();
        const rv_editor_record_result pick = rv_editor_record_row("##record",
            name.c_str(),
            parent.c_str(),
            row.there ? words[0] : words[1],
            row.there ? rv_editor_status_kind::ok : rv_editor_status_kind::warning,
            theme,
            style);
        if (pick.clicked || pick.right_clicked) {
            shell.start_selected = row.root;
        }
        if (pick.double_clicked && row.there) {
            rv_editor_shell_request_open(shell, row.root);
        }
        if (pick.right_clicked) {
            ImGui::OpenPopup("##row_menu");
        }
        rv_editor_recent_menu(shell, row);
        ImGui::PopID();
    }
}

// The selected project's disc.toml status and its buttons; the rows are there, and their buttons off with a reason,
// whatever is selected.
void rv_editor_recent_card(rv_editor_shell &shell, const rv_editor_recent_row *row, const rv_editor_theme &theme)
{
    if (row == nullptr) {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    } else if (row->there) {
        rv_editor_status(rv_editor_text("shell_start.disc_found"), rv_editor_status_kind::ok, theme);
    } else {
        rv_editor_status(rv_editor_text("shell_start.disc_missing"), rv_editor_status_kind::warning, theme);
    }
    const char *nothing = row == nullptr ? rv_editor_text("shell_start.no_recent") : nullptr;
    const char *not_there = row != nullptr && !row->there ? rv_editor_text("shell_start.not_there") : nullptr;
    if (rv_editor_button(rv_editor_text("shell_start.open_project"),
            theme,
            { rv_editor_look::live, nothing != nullptr ? nothing : not_there })) {
        rv_editor_shell_request_open(shell, row->root);
    }
    ImGui::SameLine();
    const char *found = row != nullptr && row->there ? rv_editor_text("shell_start.locate_not_needed") : nullptr;
    if (rv_editor_button(rv_editor_text("shell_start.locate"),
            theme,
            { rv_editor_look::live, nothing != nullptr ? nothing : found })) {
        rv_editor_recent_remove(row->root);
        const std::string filename_str = row->root.filename().string();
        const std::string purpose = rv_editor_text_format("shell_start.locate_dynamic", std::make_format_args(filename_str));
        rv_editor_browser_start(shell.open_browser,
            purpose,
            rv_editor_text("shell_start.open_project"),
            rv_editor_browse_pick::directory,
            row->root.parent_path());
        rv_editor_shell_open_project(shell);
    }
    ImGui::SameLine();
    // What Remove from Recent took, with the way back; the directory was never touched.
    const bool removed = !shell.recent_removed.empty();
    if (rv_editor_button(rv_editor_text("shell_start.undo"),
            theme,
            { rv_editor_look::live, removed ? nullptr : rv_editor_text("shell_start.nothing_removed") })) {
        rv_editor_recent_restore(shell.recent_removed, shell.recent_removed_at);
        shell.start_selected = shell.recent_removed;
        shell.recent_removed.clear();
    }
    if (removed) {
        const std::string name = shell.recent_removed.filename().string();
        rv_editor_item_tooltip(rv_editor_text_format("shell_start.removed_text", std::make_format_args(name)).c_str());
    }
}

} // namespace

rv_editor_size rv_editor_recent_minimum()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    return { static_cast<int32_t>(std::ceil(style.WindowPadding.x + rv_editor_recent_content_width())),
        static_cast<int32_t>(
            std::ceil(style.WindowPadding.y + rv_editor_recent_card_height() + rv_editor_recent_area_floor())) };
}

void rv_editor_page_recent(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    const std::vector<rv_editor_recent_row> rows = rv_editor_recent_rows();
    // A selection that left the list falls back to the first project.
    const auto kept = std::find_if(rows.begin(), rows.end(), [&](const rv_editor_recent_row &r) {
        return r.root == shell.start_selected;
    });
    size_t index = kept != rows.end() ? static_cast<size_t>(kept - rows.begin()) : 0;
    // While the page has the keyboard the arrows move the selection and Enter opens it.
    if (!rows.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
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
    shell.start_selected = rows.empty() ? std::filesystem::path() : rows[index].root;
    (void)rv_editor_tiled_area_begin("##recent_area",
        rv_editor_recent_content_width(),
        theme,
        { rv_editor_recent_area_floor() },
        false,
        std::max(1.0f, ImGui::GetContentRegionAvail().y - rv_editor_recent_card_height()));
    rv_editor_recent_records(shell, rows, theme);
    rv_editor_tiled_area_end(theme);
    rv_editor_recent_card(shell, rows.empty() ? nullptr : &rows[index], theme);
}

} // namespace rv_editor
