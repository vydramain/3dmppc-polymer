// New Project as a dialog of its own: the template, the name and id, the directory,
// then the path it makes and the files it will hold. A problem shows beside its field.

#include "app/rv_editor_shell.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "project/rv_editor_templates.hpp"

namespace rv_editor
{

namespace
{

constexpr const char *rv_editor_new_title = "New Project";

// SDL calls this when the Browse dialog closes, maybe on another thread.
void SDLCALL rv_editor_new_dir_picked(void *userdata, const char *const *files, int)
{
    if (files == nullptr || files[0] == nullptr) {
        return;
    }
    rv_editor_shell &shell = *static_cast<rv_editor_shell *>(userdata);
    const std::lock_guard<std::mutex> lock(shell.picked_mutex);
    shell.start.picked_dir = files[0];
}

void rv_editor_new_label(const char *label, float label_w)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(label_w);
}

// The problem under the field it is about; an empty name or id only disables Create.
void rv_editor_new_problem_row(bool shown, const std::string &problem, float label_w, const rv_editor_theme &theme)
{
    if (!shown) {
        return;
    }
    ImGui::SetCursorPosX(label_w);
    rv_editor_status(problem.c_str(), rv_editor_status_kind::error, theme);
}

} // namespace

void rv_editor_shell_new_project(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_start &f = shell.start;
    if (f.form_open) {
        ImGui::OpenPopup(rv_editor_new_title);
        f.form_open = false;
    }
    if (!rv_editor_dialog_begin(rv_editor_new_title, theme)) {
        return;
    }
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
    const std::vector<rv_editor_template> templates = rv_editor_templates();
    const rv_editor_new_project p{ f.name, f.id, f.dir, f.template_index };
    rv_editor_new_project_field at = rv_editor_new_project_field::none;
    const std::string problem = rv_editor_new_project_problem(p, templates, &at);
    const bool blank = (at == rv_editor_new_project_field::name && p.name.empty()) ||
        (at == rv_editor_new_project_field::disc_id && p.disc_id.empty());
    const auto beside = [&](rv_editor_new_project_field field) { return !blank && at == field; };

    const float label_w = ImGui::GetFontSize() * 9.0f;
    const float field_w = ImGui::GetFontSize() * 30.0f;

    rv_editor_new_label("Template", label_w);
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
    if (f.template_index < templates.size()) {
        ImGui::SetCursorPosX(label_w);
        ImGui::PushTextWrapPos(label_w + field_w);
        ImGui::TextUnformatted(templates[f.template_index].description.c_str());
        ImGui::PopTextWrapPos();
    }
    rv_editor_new_problem_row(beside(rv_editor_new_project_field::template_index), problem, label_w, theme);

    rv_editor_new_label("Name", label_w);
    ImGui::SetNextItemWidth(field_w);
    if (rv_editor_text_field("##name", f.name, sizeof(f.name), theme) && !f.id_edited) {
        const std::string id = rv_editor_disc_id_from(f.name);
        std::strncpy(f.id, id.c_str(), sizeof(f.id) - 1);
        f.id[id.size() < sizeof(f.id) ? id.size() : sizeof(f.id) - 1] = '\0';
    }

    rv_editor_new_label("Disc ID", label_w);
    ImGui::SetNextItemWidth(field_w);
    rv_editor_field id_field;
    id_field.invalid = beside(rv_editor_new_project_field::disc_id) ? problem.c_str() : nullptr;
    if (rv_editor_text_field("##id", f.id, sizeof(f.id), theme, id_field)) {
        f.id_edited = true;
    }
    rv_editor_new_problem_row(beside(rv_editor_new_project_field::disc_id), problem, label_w, theme);

    rv_editor_new_label("Directory", label_w);
    ImGui::SetNextItemWidth(field_w - rv_editor_button_width("Browse...") - ImGui::GetStyle().ItemSpacing.x);
    rv_editor_field dir_field;
    dir_field.invalid = beside(rv_editor_new_project_field::parent) ? problem.c_str() : nullptr;
    rv_editor_text_field("##dir", f.dir, sizeof(f.dir), theme, dir_field);
    ImGui::SameLine();
    if (rv_editor_button("Browse...", theme)) {
        SDL_ShowOpenFolderDialog(rv_editor_new_dir_picked, &shell, shell.window, nullptr, false);
    }
    rv_editor_new_problem_row(beside(rv_editor_new_project_field::parent), problem, label_w, theme);

    const std::string root = (std::filesystem::path(f.dir) / (p.disc_id.empty() ? "?" : p.disc_id)).string();
    rv_editor_new_label("Creates", label_w);
    ImGui::TextUnformatted(root.c_str());
    rv_editor_new_problem_row(beside(rv_editor_new_project_field::root), problem, label_w, theme);
    if (f.template_index < templates.size()) {
        rv_editor_font_code_push();
        for (const std::string &file : rv_editor_new_project_files(p, templates[f.template_index])) {
            ImGui::SetCursorPosX(label_w);
            ImGui::TextUnformatted(file.c_str());
        }
        rv_editor_font_code_pop();
    }

    if (!f.error.empty()) {
        rv_editor_status(f.error.c_str(), rv_editor_status_kind::error, theme);
    }
    ImGui::Separator();
    if (rv_editor_button("Create Project", theme, { rv_editor_look::live, problem.empty() ? nullptr : problem.c_str() })) {
        if (rv_editor_new_project_create(p, templates[f.template_index], f.error)) {
            const std::filesystem::path created = std::filesystem::path(f.dir) / p.disc_id;
            f = {};
            ImGui::CloseCurrentPopup();
            rv_editor_shell_request_open(shell, created);
        }
    }
    ImGui::SameLine();
    if (rv_editor_button("Cancel", theme) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        f = {};
        ImGui::CloseCurrentPopup();
    }
    rv_editor_dialog_end();
}

} // namespace rv_editor
