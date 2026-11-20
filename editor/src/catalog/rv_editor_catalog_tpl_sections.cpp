// Template section of the Widget Catalog: titled sections in one content area.

#include <algorithm>

#include "catalog/rv_editor_catalog_templates.hpp"
#include "ui/rv_editor_widgets_content.hpp"

namespace rv_editor
{

namespace
{

const auto T = rv_editor_catalog_tpl_text;

const char *const sections_states[] = { "st_sections_1", "st_sections_2", "st_sections_3" };

constexpr float field_ems = 8.0f;

float button_width(const char *label)
{
    return ImGui::CalcTextSize(label).x + 2.0f * ImGui::GetStyle().FramePadding.x;
}

// The field and the two buttons of the control state.
float controls_width()
{
    return field_ems * ImGui::GetFontSize() + 2.0f * ImGui::GetStyle().ItemSpacing.x + button_width(T("sections_mark")) +
        button_width(T("sections_capture"));
}

void sections_draw(int state, const rv_editor_theme &theme)
{
    const bool controls = state == 1;
    rv_editor_content_begin("##well", ImVec2(0.0f, 0.0f), theme);
    rv_editor_content_section_begin(T(state == 2 ? "sections_long" : "sections_title"),
        controls ? controls_width() : 0.0f,
        theme);
    if (controls) {
        static char note[32] = "";
        ImGui::SetNextItemWidth(field_ems * ImGui::GetFontSize());
        ImGui::InputText("##note", note, sizeof note);
        ImGui::SameLine();
        ImGui::Button(T("sections_mark"));
        ImGui::SameLine();
        ImGui::Button(T("sections_capture"));
    }
    rv_editor_content_section_end(theme);
    ImGui::TextUnformatted(T("sections_row"));
    rv_editor_content_end(theme);
}

ImVec2 sections_min(const rv_editor_theme &theme)
{
    // The widest title with the controls, then the area's frame and its vertical bar.
    const float width = std::max(rv_editor_content_section_min_width(T("sections_long"), controls_width()),
        rv_editor_content_section_min_width(T("sections_title"), controls_width()));
    return rv_editor_catalog_tpl_window(ImVec2(width + 2.0f * theme_pad(theme) + ImGui::GetStyle().ScrollbarSize,
        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + rv_editor_content_min_height()));
}

} // namespace

RV_EDITOR_TPL(sections);

} // namespace rv_editor
