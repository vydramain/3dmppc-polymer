// New Project as a page: a form (template, name, disc id, directory with Browse), its buttons, and under them one content area
// that holds the problem, the failed create or, with none, the path the project will have. Directory's Browse is the big ask's
// card over the page. What is typed stays until Reset.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "panes/rv_editor_pane_tiled.hpp"
#include "project/rv_editor_templates.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_form.hpp"
#include "ui/rv_editor_widgets_mode.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

// Stands in for the project folder name (Disc ID) in the path preview while the field is empty.
constexpr const char *disc_id_placeholder = "?";

// Environment variable name for the user's home directory.
constexpr const char *env_home = "HOME";

// The text fields are at least this wide, in font sizes.
constexpr float field_min_em = 14.0f;

// What the Directory field drew last, for tests.
rv_editor_path_probe dir_probe;

void rv_editor_new_copy(char *to, size_t size, const std::string &from)
{
    std::strncpy(to, from.c_str(), size - 1);
    to[size - 1] = '\0';
}

// The rows of the form, for its label column and its height; the rows with their own field have no error line (the
// area under the buttons says what is wrong).
std::vector<rv_editor_form_row_desc> rv_editor_new_rows()
{
    return { { rv_editor_text("shell_start_new.template"), nullptr, nullptr, false },
        { rv_editor_text("shell_start_new.name"), nullptr, nullptr, false },
        { rv_editor_text("shell_start_new.disc_id"), nullptr, nullptr, false },
        { rv_editor_text("shell_start_new.directory"), nullptr, nullptr, false } };
}

float rv_editor_new_label_column()
{
    float width = 0.0f;
    for (const auto &row : rv_editor_new_rows()) {
        width = std::max(width, ImGui::CalcTextSize(row.label).x);
    }
    return width + ImGui::GetStyle().ItemSpacing.x;
}

float rv_editor_new_radios_width(const std::vector<rv_editor_template> &templates)
{
    float width = 0.0f;
    for (const rv_editor_template &t : templates) {
        width += rv_editor_checkbox_width(t.name.c_str()) + ImGui::GetStyle().ItemSpacing.x;
    }
    return width;
}

float rv_editor_new_browse_width()
{
    return rv_editor_button_width(rv_editor_text("shell_start_new.browse")) + ImGui::GetStyle().ItemSpacing.x;
}

// The widest row: a label and the fields at their floor, the buttons, the box, or the card.
float rv_editor_new_content_width()
{
    const float label = rv_editor_new_label_column();
    const float buttons = rv_editor_button_width(rv_editor_text("shell_start_new.create_project")) +
        rv_editor_button_width(rv_editor_text("shell_start_new.reset")) + ImGui::GetStyle().ItemSpacing.x;
    const float form = label +
        std::max(rv_editor_new_radios_width(rv_editor_templates()),
            ImGui::GetFontSize() * field_min_em + rv_editor_new_browse_width());
    const float card = rv_editor_browser_card_min_width({ rv_editor_text("shell_start_new.select_dir_for_new") },
        rv_editor_text("shell_start_new.select_dir_title"));
    return std::max({ form, buttons, card });
}

// The files a template makes under a disc id, kept until either changes: listing them reads the template's directory.
const std::vector<std::string> &rv_editor_new_files_cached(const rv_editor_new_project &p, const rv_editor_template &t)
{
    static std::string key;
    static std::vector<std::string> files;
    const std::string now = t.id + '\n' + p.disc_id;
    if (now != key) {
        key = now;
        files = rv_editor_new_project_files(p, t);
    }
    return files;
}

} // namespace

rv_editor_size rv_editor_new_project_minimum()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float form =
        rv_editor_form_min_height(rv_editor_new_rows()) + ImGui::GetStyle().ItemSpacing.y + rv_editor_tiled_floor();
    const float card =
        rv_editor_mode_card_min_size({ rv_editor_text("shell_start_new.select_dir_title"), rv_editor_text("browser.cancel") },
            rv_editor_theme_olive)
            .y;
    return { static_cast<int32_t>(std::ceil(style.WindowPadding.x + rv_editor_new_content_width())),
        static_cast<int32_t>(std::ceil(style.WindowPadding.y + std::max(form, card))) };
}

void rv_editor_page_new_project(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_start &f = shell.start;
    // The home directory until one is chosen.
    if (f.dir[0] == '\0') {
        const char *home = std::getenv(env_home);
        rv_editor_new_copy(f.dir, sizeof(f.dir), home != nullptr ? home : "");
    }
    // Browsing is a mode of the page: the card takes the page's place and the form comes back after it.
    if (f.browsing) {
        std::filesystem::path picked;
        const rv_editor_browse_result r = rv_editor_browser_card(f.browser, picked, theme);
        if (r == rv_editor_browse_result::picked) {
            rv_editor_new_copy(f.dir, sizeof(f.dir), picked.string());
        }
        f.browsing = r == rv_editor_browse_result::none;
        return;
    }
    const std::vector<rv_editor_template> templates = rv_editor_templates();
    const rv_editor_new_project p{ f.name, f.id, f.dir, f.template_index };
    rv_editor_new_project_field at = rv_editor_new_project_field::none;
    const std::string problem = rv_editor_new_project_problem(p, templates, &at);
    // An empty name or id only turns Create off; the box does not scold a form not yet begun.
    const bool blank = (at == rv_editor_new_project_field::name && p.name.empty()) ||
        (at == rv_editor_new_project_field::disc_id && p.disc_id.empty());
    const auto beside = [&](rv_editor_new_project_field field) {
        return !blank && at == field;
    };

    const std::vector<rv_editor_form_row_desc> rows = rv_editor_new_rows();
    rv_editor_form_begin(rows, theme);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rows[0].label);
    ImGui::SameLine(rv_editor_form_field_x());
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
    if (rv_editor_form_row(rows[1].label, f.name, sizeof(f.name), nullptr, false, nullptr, theme) && !f.id_edited) {
        rv_editor_new_copy(f.id, sizeof(f.id), rv_editor_disc_id_from(f.name));
    }
    rv_editor_field id_field;
    id_field.invalid = beside(rv_editor_new_project_field::disc_id) ? problem.c_str() : nullptr;
    if (rv_editor_form_row(rows[2].label, f.id, sizeof(f.id), nullptr, false, nullptr, theme, id_field)) {
        f.id_edited = true;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rows[3].label);
    ImGui::SameLine(rv_editor_form_field_x());
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - rv_editor_new_browse_width(), ImGui::GetFontSize()));
    rv_editor_field dir_field;
    const bool dir_problem = beside(rv_editor_new_project_field::parent) || beside(rv_editor_new_project_field::root) ||
        beside(rv_editor_new_project_field::template_index);
    dir_field.invalid = dir_problem ? problem.c_str() : nullptr;
    rv_editor_path_field("##dir", f.dir, sizeof(f.dir), "", theme, dir_field, &dir_probe);
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("shell_start_new.browse"), theme)) {
        rv_editor_browser_start(f.browser,
            rv_editor_text("shell_start_new.select_dir_for_new"),
            rv_editor_text("shell_start_new.select_dir_title"),
            rv_editor_browse_pick::directory,
            f.dir);
        f.browsing = true;
    }

    rv_editor_form_buttons_begin(theme);
    const char *why_not = rv_editor_shell_why_not_project_switch(shell);
    if (why_not == nullptr && !problem.empty()) {
        why_not = problem.c_str();
    }
    if (rv_editor_button(rv_editor_text("shell_start_new.create_project"), theme, { rv_editor_look::live, why_not })) {
        if (rv_editor_new_project_create(p, templates[f.template_index], f.error) == RV_OK) {
            const std::filesystem::path created = std::filesystem::path(f.dir) / p.disc_id;
            f = {};
            rv_editor_shell_request_open(shell, created);
        }
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("shell_start_new.reset"), theme)) {
        f = {};
    }
    rv_editor_form_end();

    // A failed create, else the problem with a field, else where the project will go: in the area under the buttons, whose
    // frame is the same whatever it says.
    const std::string root = (p.parent / (p.disc_id.empty() ? disc_id_placeholder : p.disc_id)).string();
    const std::string creates = std::string(rv_editor_text("shell_start_new.creates")) + ": " + root;
    const bool wrong = !f.error.empty() || (!blank && !problem.empty());
    (void)rv_editor_tiled_area_begin("##new_area", rv_editor_new_content_width(), theme);
    rv_editor_label(!f.error.empty() ? f.error.c_str() : (wrong ? problem.c_str() : creates.c_str()),
        wrong ? rv_editor_label_kind::error : rv_editor_label_kind::normal,
        theme,
        rv_editor_label_layout::wrap);
    if (f.template_index < templates.size()) {
        rv_editor_text_item(templates[f.template_index].description.c_str());
        // The names carry the disc id, so they are listed only for an id that is settled: typing does not reshuffle the page.
        if (!wrong && !blank) {
            for (const std::string &file : rv_editor_new_files_cached(p, templates[f.template_index])) {
                rv_editor_text_item(file.c_str());
            }
        }
    }
    rv_editor_tiled_area_end(theme);
}

const rv_editor_path_probe &rv_editor_page_new_project_dir_probe()
{
    return dir_probe;
}

} // namespace rv_editor
