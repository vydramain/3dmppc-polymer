// The window with no project open (PRJ-07): New Project, Open Project and the
// recent projects, instead of a grid of empty tiles.

#include "app/rv_editor_shell.hpp"

#include <system_error>

#include "imgui.h"

#include "project/rv_editor_templates.hpp"

namespace rv_editor
{

namespace
{

void rv_editor_start_recent(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    ImGui::SeparatorText("Recent Projects");
    const std::vector<std::filesystem::path> recent = rv_editor_recent_load();
    if (recent.empty()) {
        ImGui::TextWrapped("None yet: a project opened here is listed next time.");
        return;
    }
    if (!ImGui::BeginTable("##recent", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Directory");
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (const std::filesystem::path &root : recent) {
        ImGui::PushID(root.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(root.filename().c_str());
        ImGui::TableNextColumn();
        std::error_code ec;
        const bool there = std::filesystem::exists(root / "disc.toml", ec);
        ImGui::TextUnformatted(root.c_str());
        if (!there) {
            // Say why and offer the way on; the directory itself is never touched.
            rv_editor_status("missing: no disc.toml there any more", rv_editor_status_kind::warning, theme);
        }
        ImGui::TableNextColumn();
        if (rv_editor_button("Open", theme, { rv_editor_look::live, there ? nullptr : "The project is not there" })) {
            rv_editor_shell_request_open(shell, root);
        }
        ImGui::SameLine();
        if (!there && rv_editor_button("Locate...", theme)) {
            rv_editor_recent_remove(root);
            rv_editor_shell_open_folder(shell);
        }
        ImGui::SameLine();
        if (rv_editor_button("Forget", theme)) {
            rv_editor_recent_remove(root);
        }
        ImGui::SetItemTooltip("Takes it off this list; the directory stays as it is");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace

void rv_editor_shell_start_screen(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header("3dmppc-editor", true, theme);
    const float pad = static_cast<float>(theme.pad_px) * theme.scale * 3.0f;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + pad, ImGui::GetCursorPosY() + pad));
    ImGui::BeginGroup();
    ImGui::TextUnformatted("No project is open. A game's directory, with its disc.toml, is the workspace.");
    ImGui::Dummy(ImVec2(0.0f, pad));
    const float column = ImGui::GetFontSize() * 14.0f;
    ImGui::BeginChild("##start_commands", ImVec2(column, 0.0f), ImGuiChildFlags_AutoResizeY);
    if (rv_editor_command_button("##new", 'N', theme.code_green, "New Project...", "A new disc from a starting template", theme)) {
        shell.start.form_open = true;
    }
    if (rv_editor_command_button("##open", 'O', 0x958831, "Open Project...", "A game directory with a disc.toml", theme)) {
        rv_editor_shell_open_folder(shell);
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, pad);
    ImGui::BeginChild("##start_body", ImVec2(0.0f, 0.0f));
    rv_editor_start_recent(shell, theme);
    ImGui::EndChild();
    ImGui::EndGroup();
    rv_editor_shell_new_project(shell, theme);
}

} // namespace rv_editor
