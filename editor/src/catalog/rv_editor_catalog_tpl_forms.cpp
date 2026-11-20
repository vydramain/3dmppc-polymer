// Template sections of the Widget Catalog, part two: the big ask, the form and the transport strip.

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "catalog/rv_editor_catalog_templates.hpp"
#include "panes/rv_editor_pane_controls.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_form.hpp"
#include "ui/rv_editor_widgets_mode.hpp"
#include "ui/rv_editor_widgets_strip_status.hpp"

namespace rv_editor
{

namespace
{

const auto T = rv_editor_catalog_tpl_text;

// --- big ask ------------------------------------------------------------------

const char *const mode_states[] = { "st_mode_1", "st_mode_2", "st_mode_3", "st_mode_4" };

std::vector<std::string> mode_buttons()
{
    return { T("mode_save"), T("mode_discard"), T("mode_keep_open") };
}

void mode_draw(int state, const rv_editor_theme &theme)
{
    if (state == 0) {
        rv_editor_content_begin("##well", ImVec2(0.0f, 0.0f), theme);
        ImGui::TextUnformatted(T("mode_working"));
        rv_editor_content_end(theme);
        return;
    }
    rv_editor_mode_card_spec spec;
    spec.title = T("mode_title");
    spec.text = state == 2 ? T("mode_text_long") : T("mode_text");
    spec.message = state == 3 ? T("mode_failed") : "";
    spec.buttons = mode_buttons();
    rv_editor_mode_card_begin("##card", spec, theme);
    rv_editor_mode_card_end();
}

ImVec2 mode_min(const rv_editor_theme &theme)
{
    const ImVec2 card = rv_editor_mode_card_min_size(mode_buttons(), theme);
    return rv_editor_catalog_tpl_window(card);
}

// --- form ---------------------------------------------------------------------

const char *const form_states[] = { "st_form_1", "st_form_2", "st_form_3", "st_form_4" };

constexpr int form_rows = 3;
constexpr size_t form_buffer = 128;

// The values of a state; the long ones are the widest, so the minimum is computed from them.
std::vector<rv_editor_form_row_desc> form_descs(const char *const values[form_rows])
{
    const char *const labels[form_rows] = { T("form_label_name"), T("form_label_runtime"), T("form_label_dir") };
    const char *const hints[form_rows] = { T("form_hint_name"), T("form_hint_runtime"), T("form_hint_dir") };
    std::vector<rv_editor_form_row_desc> rows;
    for (int i = 0; i < form_rows; ++i) {
        rows.push_back({ labels[i], values[i], hints[i], i == 1 });
    }
    return rows;
}

void form_values(int state, const char *out[form_rows])
{
    const char *const normal[form_rows] = { T("form_value_name"), T("form_value_runtime"), T("form_value_dir") };
    const char *const longer[form_rows] = { T("form_long_name"), T("form_long_runtime"), T("form_long_dir") };
    for (int i = 0; i < form_rows; ++i) {
        out[i] = state == 3 ? "" : state == 1 ? longer[i] : normal[i];
    }
}

void form_draw(int state, const rv_editor_theme &theme)
{
    const char *values[form_rows];
    form_values(state, values);
    const std::vector<rv_editor_form_row_desc> rows = form_descs(values);
    char buffers[form_rows][form_buffer];
    rv_editor_form_begin(rows, theme);
    for (int i = 0; i < form_rows; ++i) {
        std::strncpy(buffers[i], values[i], form_buffer - 1);
        buffers[i][form_buffer - 1] = '\0';
        rv_editor_form_row(rows[i].label,
            buffers[i],
            form_buffer,
            rows[i].hint,
            rows[i].can_fail,
            state == 2 && i == 1 ? T("form_error") : nullptr,
            theme);
    }
    rv_editor_form_buttons_begin(theme);
    rv_editor_button(T("form_apply"), theme);
    ImGui::SameLine();
    rv_editor_button(T("form_revert"), theme, { .disabled = T("form_revert_why") });
    rv_editor_form_end();
}

ImVec2 form_min(const rv_editor_theme &)
{
    const char *values[form_rows];
    form_values(1, values);
    const std::vector<rv_editor_form_row_desc> rows = form_descs(values);
    return rv_editor_catalog_tpl_window(ImVec2(rv_editor_form_min_width(rows), rv_editor_form_min_height(rows)));
}

// --- transport ----------------------------------------------------------------

const char *const transport_states[] = { "st_transport_1",
    "st_transport_2",
    "st_transport_3",
    "st_transport_4",
    "st_transport_5" };

void transport_draw(int state, const rv_editor_theme &theme)
{
    const char *nothing = T("transport_why_no_build");
    const char *none = rv_editor_text("catalog_status.transport_nothing_running");
    const bool building = state == 1;
    rv_editor_transport_state buttons = { nullptr, nullptr, none, none, none, none };
    if (building) {
        buttons = { T("transport_why_building"), T("transport_why_building"), none, none, none, none };
    } else if (state == 2) {
        buttons = { nullptr, T("transport_why_running"), nullptr, none, nullptr, nullptr };
    } else if (state == 3) {
        buttons = { nullptr, nullptr, rv_editor_text("catalog_status.transport_already_paused"), nullptr, nullptr, nullptr };
        buttons.resume = true;
    }
    const char *const messages[] = { T("transport_idle"),
        T("transport_building"),
        T("transport_running"),
        T("transport_paused"),
        T("transport_failed") };
    rv_editor_shelf_begin("##strip", theme);
    rv_editor_transport_bar(buttons, theme, rv_editor_transport_column_min_width());
    ImGui::SameLine();
    ImGui::BeginGroup();
    rv_editor_button(rv_editor_text("panes.cancel_build_button"), theme, { .disabled = building ? nullptr : nothing });
    ImGui::EndGroup();
    rv_editor_strip_status(messages[state],
        std::max(rv_editor_transport_status_min_width(), ImGui::GetContentRegionAvail().x),
        theme);
    rv_editor_shelf_end();
}

ImVec2 transport_min(const rv_editor_theme &)
{
    // The strip's measures count the window's left padding only: its right column reaches the right edge.
    const float width = rv_editor_transport_min_width() + ImGui::GetStyle().WindowPadding.x;
    return ImVec2(width, rv_editor_transport_height(width) + ImGui::GetStyle().WindowPadding.y);
}

ImVec2 transport_roomy(const rv_editor_theme &theme)
{
    // Wide enough for the large buttons (from 640 px), which makes the strip taller.
    float width = transport_min(theme).x * 1.5f;
    while (
        rv_editor_transport_side(width, rv_editor_transport_column_min_width()) <= ImGui::GetFrameHeight() && width < 4000.0f) {
        width += 20.0f;
    }
    return ImVec2(width, rv_editor_transport_height(width) + ImGui::GetStyle().WindowPadding.y);
}

} // namespace

RV_EDITOR_TPL(mode);
RV_EDITOR_TPL(form);

const rv_editor_catalog_template rv_editor_catalog_template_transport = { "section_transport",
    transport_states,
    static_cast<int>(std::size(transport_states)),
    transport_min,
    transport_roomy,
    transport_draw };

} // namespace rv_editor
