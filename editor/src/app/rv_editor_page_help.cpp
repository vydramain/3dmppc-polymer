// Help page: keyboard shortcuts filtered by user input.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

void rv_editor_page_help(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header("Help: Keyboard Shortcuts", true, theme);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filter");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
    rv_editor_text_field("##filter", shell.help_filter, sizeof(shell.help_filter), theme);
    // A row stays when its command or its keys hold the filter, case aside.
    const auto holds = [&](const char *text) {
        const std::string_view f = shell.help_filter;
        const std::string_view t = text;
        return std::search(t.begin(), t.end(), f.begin(), f.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        }) != t.end();
    };
    // Grouped by the menu that holds each command, in menu-bar order; then by
    // where a shortcut with no menu works. Within a group, the menu's own order.
    struct rv_editor_help_row
    {
        const char *group;
        const char *label;
        const char *keys;
    };
    constexpr rv_editor_help_row rows[] = {
        { "File", "Save / Save All", "Ctrl+S / Ctrl+Shift+S" },
        { "Edit", "Undo / Redo", "Ctrl+Z / Ctrl+Shift+Z" },
        { "Edit", "Find in Project", "Ctrl+Shift+F" },
        { "Project", "Build", "Ctrl+B" },
        { "Run", "Run / Resume", "F5" },
        { "Run", "Pause", "F6" },
        { "Run", "Step Frame", "F7" },
        { "Run", "Stop", "Shift+F5" },
        { "Run", "Reload", "F8" },
        { "Window", "Focus Next / Previous Pane", "Ctrl+F6 / Ctrl+Shift+F6" },
        { "Game", "Release Game input", "Shift+Esc" },
        { "Code", "Vim mode in a code tile", "F2" },
        { "Help", "Manual", "F1" },
    };
    if (!ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        return;
    }
    const char *group = nullptr;
    for (const auto &r : rows) {
        if (!holds(r.label) && !holds(r.keys)) {
            continue;
        }
        if (group == nullptr || std::string_view(group) != r.group) {
            group = r.group;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
            ImGui::TextUnformatted(group);
            ImGui::PopStyleColor();
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.label);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.keys);
    }
    ImGui::EndTable();
}

} // namespace rv_editor
