// The window with no project open (PRJ-07): New Project, Open Directory, Open
// disc.toml and the recent projects, instead of a grid of empty tiles.

#include "app/rv_editor_shell.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "project/rv_editor_templates.hpp"

namespace rv_editor
{

namespace
{

// SDL calls this when the Browse dialog closes, maybe on another thread.
void SDLCALL rv_editor_start_dir_picked(void *userdata, const char *const *files, int)
{
    if (files == nullptr || files[0] == nullptr) {
        return;
    }
    rv_editor_shell &shell = *static_cast<rv_editor_shell *>(userdata);
    const std::lock_guard<std::mutex> lock(shell.picked_mutex);
    shell.start.picked_dir = files[0];
}

void rv_editor_start_form(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_start &f = shell.start;
    const std::vector<rv_editor_template> templates = rv_editor_templates();
    {
        const std::lock_guard<std::mutex> lock(shell.picked_mutex);
        if (!f.picked_dir.empty()) {
            std::strncpy(f.dir, f.picked_dir.c_str(), sizeof(f.dir) - 1);
            f.picked_dir.clear();
        }
    }
    // The home directory until one is chosen.
    if (f.dir[0] == '\0') {
        const char *home = std::getenv("HOME");
        std::strncpy(f.dir, home != nullptr ? home : "", sizeof(f.dir) - 1);
    }
    ImGui::SeparatorText("New Project");
    const float label_w = ImGui::GetFontSize() * 7.0f;
    const float field_w = ImGui::GetFontSize() * 26.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(field_w);
    if (rv_editor_text_field("##name", f.name, sizeof(f.name), theme) && !f.id_edited) {
        const std::string id = rv_editor_disc_id_from(f.name);
        std::strncpy(f.id, id.c_str(), sizeof(f.id) - 1);
        f.id[id.size() < sizeof(f.id) ? id.size() : sizeof(f.id) - 1] = '\0';
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Disc ID");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(field_w);
    if (rv_editor_text_field("##id", f.id, sizeof(f.id), theme)) {
        f.id_edited = true;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Directory");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(field_w - rv_editor_button_width("Browse...") - ImGui::GetStyle().ItemSpacing.x);
    rv_editor_text_field("##dir", f.dir, sizeof(f.dir), theme);
    ImGui::SameLine();
    if (rv_editor_button("Browse...", theme)) {
        SDL_ShowOpenFolderDialog(rv_editor_start_dir_picked, &shell, shell.window, nullptr, false);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Template");
    ImGui::SameLine(label_w);
    for (size_t i = 0; i < templates.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(static_cast<int>(i));
        if (rv_editor_radio(templates[i].name.c_str(), f.template_index == i, theme)) {
            f.template_index = i;
        }
        ImGui::PopID();
    }
    rv_editor_new_project p{ f.name, f.id, f.dir, f.template_index };
    if (f.template_index < templates.size()) {
        ImGui::TextWrapped("%s", templates[f.template_index].description.c_str());
        ImGui::SeparatorText("It will hold");
        const std::string root = (std::filesystem::path(f.dir) / (p.disc_id.empty() ? "?" : p.disc_id)).string();
        ImGui::TextUnformatted(root.c_str());
        rv_editor_font_code_push();
        for (const std::string &file : rv_editor_new_project_files(p, templates[f.template_index])) {
            ImGui::Text("  %s", file.c_str());
        }
        rv_editor_font_code_pop();
    }
    const std::string problem = rv_editor_new_project_problem(p, templates);
    if (!f.error.empty()) {
        rv_editor_status(f.error.c_str(), rv_editor_status_kind::error, theme);
    }
    if (rv_editor_button("Create Project", theme, { rv_editor_look::live, problem.empty() ? nullptr : problem.c_str() })) {
        if (rv_editor_new_project_create(p, templates[f.template_index], f.error)) {
            const std::filesystem::path root = std::filesystem::path(f.dir) / p.disc_id;
            f = {};
            rv_editor_shell_request_open(shell, root);
            return;
        }
    }
    ImGui::SameLine();
    if (rv_editor_button("Cancel", theme)) {
        f = {};
    }
}

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
    if (rv_editor_command_button("##open", 'D', 0x958831, "Open Directory...", "A game directory with a disc.toml", theme)) {
        rv_editor_shell_open_folder(shell);
    }
    if (rv_editor_command_button("##manifest", 'M', 0xfab387, "Open disc.toml...", "A disc.toml: its directory opens",
            theme)) {
        rv_editor_shell_open_manifest(shell);
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, pad);
    ImGui::BeginChild("##start_body", ImVec2(0.0f, 0.0f));
    if (shell.start.form_open) {
        rv_editor_start_form(shell, theme);
    } else {
        rv_editor_start_recent(shell, theme);
    }
    ImGui::EndChild();
    ImGui::EndGroup();
}

} // namespace rv_editor
