// Template sections of the Widget Catalog, part three: the picture, the facts list and the status bar.

#include <algorithm>
#include <string>
#include <vector>

#include "catalog/rv_editor_catalog_templates.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_facts.hpp"
#include "ui/rv_editor_widgets_picture.hpp"

namespace rv_editor
{

namespace
{

const auto T = rv_editor_catalog_tpl_text;

// --- picture ------------------------------------------------------------------

const char *const picture_states[] = { "st_picture_1", "st_picture_2", "st_picture_3", "st_picture_4" };

constexpr int frame_w = 320;
constexpr int frame_h = 240;

std::vector<std::string> picture_texts()
{
    return { T("picture_stopped"), T("picture_running"), T("picture_stale"), T("picture_note") };
}

void picture_draw(int state, const rv_editor_theme &theme)
{
    rv_editor_shelf_begin("##modes", theme);
    const char *modes[] = { T("picture_fit"), T("picture_integer"), T("picture_1x"), T("picture_2x"), T("picture_3x") };
    for (const char *mode : modes) {
        rv_editor_button(mode, theme);
        ImGui::SameLine();
    }
    rv_editor_button(T("picture_run"), theme, { .disabled = state == 1 ? T("transport_why_running") : nullptr });
    rv_editor_shelf_end();
    const rv_editor_picture_rect area = rv_editor_picture_begin("##picture", theme, false);
    if (state != 0) {
        const rv_editor_picture_rect frame = rv_editor_picture_frame(area, frame_w, frame_h);
        ImGui::GetWindowDrawList()->AddRectFilled(frame.min, frame.max, ImGui::GetColorU32(ImGuiCol_FrameBg));
    }
    rv_editor_picture_overlay(picture_texts()[state],
        state >= 2 ? rv_editor_label_kind::warning : rv_editor_label_kind::normal,
        theme,
        area);
    rv_editor_picture_end();
}

ImVec2 picture_min(const rv_editor_theme &theme)
{
    const char *labels[] = { T("picture_fit"),
        T("picture_integer"),
        T("picture_1x"),
        T("picture_2x"),
        T("picture_3x"),
        T("picture_run") };
    float row = 2.0f * theme_pad(theme) + 1.0f + 5.0f * ImGui::GetStyle().ItemSpacing.x;
    for (const char *label : labels) {
        row += rv_editor_button_width(label);
    }
    return rv_editor_catalog_tpl_window(ImVec2(std::max(row, rv_editor_picture_min_width(picture_texts())),
        ImGui::GetFrameHeight() + 2.0f * theme_pad(theme) + ImGui::GetStyle().ItemSpacing.y +
            2.0f * ImGui::GetTextLineHeightWithSpacing()));
}

// --- facts --------------------------------------------------------------------

const char *const facts_states[] = { "st_facts_1", "st_facts_2", "st_facts_3" };

void facts_draw(int state, const rv_editor_theme &theme)
{
    const bool missing = state == 1;
    const bool longer = state == 2;
    rv_editor_facts_begin("##facts", ImVec2(0.0f, 0.0f), theme);
    rv_editor_facts_row(T("fact_key_candidate"), T("fact_candidate"), theme);
    rv_editor_facts_row(T("fact_key_sha"), T("fact_sha"), theme);
    rv_editor_facts_row(T("fact_key_size"), T("fact_size"), theme);
    rv_editor_facts_row(T("fact_key_revision"), missing ? nullptr : T("fact_revision"), theme);
    rv_editor_facts_row(T("fact_key_image"), longer ? T("fact_image_long") : T("fact_image"), theme);
    rv_editor_facts_row(T("fact_key_built"), longer ? T("fact_built_long") : T("fact_built"), theme);
    rv_editor_facts_row(T("fact_key_playtest"), missing ? nullptr : T("fact_playtest"), theme);
    rv_editor_facts_end(theme);
}

ImVec2 facts_min(const rv_editor_theme &)
{
    return rv_editor_catalog_tpl_window(ImVec2(16.0f * ImGui::GetFontSize(), rv_editor_content_min_height()));
}

// --- status bar ---------------------------------------------------------------

const char *const status_states[] = { "st_status_1",
    "st_status_2",
    "st_status_3",
    "st_status_4",
    "st_status_5",
    "st_status_6" };

constexpr int status_segments = 4;

void status_widest(const char *out[status_segments])
{
    out[0] = T("status_project");
    out[1] = T("status_widest_tools");
    out[2] = T("status_widest_build");
    out[3] = T("status_widest_runtime");
}

void status_draw(int state, const rv_editor_theme &theme)
{
    const char *tools[] = { T("status_tools_ready"),
        T("status_tools_ready"),
        T("status_tools_ready"),
        T("status_tools_ready"),
        T("status_tools_missing"),
        T("status_tools_huge") };
    const char *build[] = { T("status_build_idle"),
        T("status_build_running"),
        T("status_build_failed"),
        T("status_build_idle"),
        T("status_build_idle"),
        T("status_build_huge") };
    const char *runtime[] = { T("status_runtime_idle"),
        T("status_runtime_idle"),
        T("status_runtime_idle"),
        T("status_runtime_running"),
        T("status_runtime_idle"),
        T("status_runtime_huge") };
    const char *fields[status_segments] = { T("status_project"), tools[state], build[state], runtime[state] };
    const char *widest[status_segments];
    status_widest(widest);
    rv_editor_status_bar(fields, widest, status_segments, theme);
}

ImVec2 status_min(const rv_editor_theme &theme)
{
    const char *widest[status_segments];
    status_widest(widest);
    return ImVec2(rv_editor_status_bar_min_width(widest, status_segments, theme),
        // A window is never lower than the style's minimum.
        std::max(ImGui::GetFrameHeight() + 2.0f * ImGui::GetStyle().WindowPadding.y, ImGui::GetStyle().WindowMinSize.y));
}

} // namespace

RV_EDITOR_TPL(picture);
RV_EDITOR_TPL(facts);
RV_EDITOR_TPL(status);

} // namespace rv_editor
