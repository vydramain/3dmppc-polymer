// New Project as a page beside the Toolchest: the form on the left, the template,
// the path it makes and its files on the right (under it when narrow). Directory's
// Browse opens the browser in the right column. What is typed stays until Reset.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "project/rv_editor_templates.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

// Stands in for the project folder name (Disc ID) in the path preview while the field is empty.
constexpr const char *disc_id_placeholder = "?";

// Environment variable name for the user's home directory.
constexpr const char *env_home = "HOME";

// New project form width in font size units.
constexpr float form_width_em = 44.0f;

// Room the preview needs beside the form for the side-by-side layout, in font size units.
constexpr float wide_threshold_em = 30.0f;

// Label column width in font size units.
constexpr float label_width_em = 9.0f;

// Form width below which the labels go over their fields, in font size units.
constexpr float stacked_below_em = 30.0f;

// Spacing between form and preview in font size units.
constexpr float side_spacing_em = 2.0f;

// What the Directory field drew last, for tests.
rv_editor_path_probe dir_probe;

// Indent of the file names under the creates path, so they read as its contents.
constexpr const char *file_indent = "  ";

void rv_editor_new_label(const char *label, float label_w, const rv_editor_theme &theme)
{
    ImGui::AlignTextToFramePadding();
    rv_editor_label(label, rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
    if (label_w > 0.0f) {
        ImGui::SameLine(label_w);
    }
}

// The problem under the field it is about, in a line that is there without it; an empty name or id only disables
// Create.
void rv_editor_new_problem_row(const char *id,
    bool shown,
    const std::string &problem,
    float label_w,
    float width,
    const rv_editor_theme &theme)
{
    ImGui::SetCursorPosX(label_w);
    rv_editor_message_line(id, shown ? problem.c_str() : "", 1, width, theme, nullptr, rv_editor_label_kind::error);
}

void rv_editor_new_copy(char *to, size_t size, const std::string &from)
{
    std::strncpy(to, from.c_str(), size - 1);
    to[size - 1] = '\0';
}

// The template's description, the path it makes and the files it will hold: quieter than the form.
void rv_editor_new_preview(const rv_editor_new_project &p,
    const std::vector<rv_editor_template> &templates,
    size_t index,
    const rv_editor_theme &theme)
{
    if (index >= templates.size()) {
        return;
    }
    rv_editor_label(templates[index].name.c_str(), rv_editor_label_kind::bright, theme);
    rv_editor_label(templates[index].description.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
    ImGui::Spacing();
    const std::string root = (p.parent / (p.disc_id.empty() ? disc_id_placeholder : p.disc_id)).string();
    rv_editor_label(rv_editor_text("shell_start_new.creates"), rv_editor_label_kind::normal, theme);
    rv_editor_label(root.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
    ImGui::Spacing();
    for (const std::string &file : rv_editor_new_project_files(p, templates[index])) {
        const std::string line = file_indent + file;
        rv_editor_label(line.c_str(), rv_editor_label_kind::dim, theme);
    }
}

} // namespace

void rv_editor_page_new_project(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_start &f = shell.start;
    // The home directory until one is chosen.
    if (f.dir[0] == '\0') {
        const char *home = std::getenv(env_home);
        rv_editor_new_copy(f.dir, sizeof(f.dir), home != nullptr ? home : "");
    }
    const std::vector<rv_editor_template> templates = rv_editor_templates();
    const rv_editor_new_project p{ f.name, f.id, f.dir, f.template_index };
    rv_editor_new_project_field at = rv_editor_new_project_field::none;
    const std::string problem = rv_editor_new_project_problem(p, templates, &at);
    const bool blank = (at == rv_editor_new_project_field::name && p.name.empty()) ||
        (at == rv_editor_new_project_field::disc_id && p.disc_id.empty());
    const auto beside = [&](rv_editor_new_project_field field) {
        return !blank && at == field;
    };

    rv_editor_pane_header(rv_editor_text("shell_start_new.title"), true, theme);
    const float font = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool wide = avail >= font * (form_width_em + wide_threshold_em);
    const float form_w = wide ? font * form_width_em : avail;
    // A narrow tile puts each label over its field.
    const bool stacked = form_w < font * stacked_below_em;
    const float label_w = stacked ? 0.0f : font * label_width_em;
    const float field_w = form_w - label_w - font;

    // A group, not a child: a child that sizes itself takes a frame to find its height.
    ImGui::BeginGroup();
    rv_editor_new_label(rv_editor_text("shell_start_new.template"), label_w, theme);
    for (size_t i = 0; i < templates.size(); ++i) {
        if (i > 0) {
            rv_editor_flow(rv_editor_checkbox_width(templates[i].name.c_str()));
        }
        ImGui::PushID(static_cast<int>(i));
        if (rv_editor_radio(templates[i].name.c_str(), f.template_index == i, theme)) {
            f.template_index = i;
        }
        ImGui::PopID();
    }

    rv_editor_new_label(rv_editor_text("shell_start_new.name"), label_w, theme);
    ImGui::SetNextItemWidth(field_w);
    if (rv_editor_text_field("##name", f.name, sizeof(f.name), theme) && !f.id_edited) {
        rv_editor_new_copy(f.id, sizeof(f.id), rv_editor_disc_id_from(f.name));
    }

    rv_editor_new_label(rv_editor_text("shell_start_new.disc_id"), label_w, theme);
    ImGui::SetNextItemWidth(field_w);
    rv_editor_field id_field;
    id_field.invalid = beside(rv_editor_new_project_field::disc_id) ? problem.c_str() : nullptr;
    if (rv_editor_text_field("##id", f.id, sizeof(f.id), theme, id_field)) {
        f.id_edited = true;
    }
    rv_editor_new_problem_row("##id_problem",
        beside(rv_editor_new_project_field::disc_id),
        problem,
        label_w,
        form_w - label_w,
        theme);

    rv_editor_new_label(rv_editor_text("shell_start_new.directory"), label_w, theme);
    const float browse_w = rv_editor_button_width(rv_editor_text("shell_start_new.browse")) + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(stacked ? field_w : field_w - browse_w);
    rv_editor_field dir_field;
    dir_field.invalid = beside(rv_editor_new_project_field::parent) ? problem.c_str() : nullptr;
    rv_editor_path_field("##dir", f.dir, sizeof(f.dir), "", theme, dir_field, &dir_probe);
    if (!stacked) {
        ImGui::SameLine();
    }
    if (rv_editor_button(rv_editor_text("shell_start_new.browse"),
            theme,
            { rv_editor_look::live, f.browsing ? rv_editor_text("shell_start_new.browser_open") : nullptr })) {
        rv_editor_browser_start(f.browser,
            rv_editor_text("shell_start_new.select_dir_for_new"),
            rv_editor_text("shell_start_new.select_dir_title"),
            rv_editor_browse_pick::directory,
            f.dir);
        f.browsing = true;
    }
    const bool dir_problem = beside(rv_editor_new_project_field::parent) || beside(rv_editor_new_project_field::root) ||
        beside(rv_editor_new_project_field::template_index);
    rv_editor_new_problem_row("##dir_problem", dir_problem, problem, label_w, form_w - label_w, theme);

    rv_editor_message_line("##new_error", f.error.c_str(), 2, form_w, theme, nullptr, rv_editor_label_kind::error);
    ImGui::Spacing();
    if (rv_editor_button(rv_editor_text("shell_start_new.create_project"),
            theme,
            { rv_editor_look::live, problem.empty() ? nullptr : problem.c_str() })) {
        if (rv_editor_new_project_create(p, templates[f.template_index], f.error) == RV_OK) {
            const std::filesystem::path created = std::filesystem::path(f.dir) / p.disc_id;
            f = {};
            rv_editor_shell_request_open(shell, created);
        }
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("shell_start_new.reset")));
    if (rv_editor_button(rv_editor_text("shell_start_new.reset"), theme)) {
        f = {};
    }
    ImGui::EndGroup();

    if (wide) {
        ImGui::SameLine(0.0f, font * side_spacing_em);
    }
    rv_editor_well_begin("##new_side", ImVec2(0.0f, 0.0f), theme);
    if (f.browsing) {
        std::filesystem::path picked;
        const rv_editor_browse_result r = rv_editor_browser_draw(f.browser, 0.0f, nullptr, picked, theme);
        if (r == rv_editor_browse_result::picked) {
            rv_editor_new_copy(f.dir, sizeof(f.dir), picked.string());
        }
        if (r != rv_editor_browse_result::none) {
            f.browsing = false;
        }
    } else {
        rv_editor_new_preview(p, templates, f.template_index, theme);
    }
    rv_editor_well_end();
}

const rv_editor_path_probe &rv_editor_page_new_project_dir_probe()
{
    return dir_probe;
}

} // namespace rv_editor
