// The form widget: see rv_editor_widgets_form.hpp.

#include "ui/rv_editor_widgets_form.hpp"

#include <algorithm>
#include <map>
#include <string>

#include "imgui_internal.h"
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

// The drawn form's label column; a form is never nested, so one state is enough.
struct form_state {
    float start_x = 0.0f;
    float field_x = 0.0f;
    std::map<std::string, float> field_widths; // by label: what a custom row's item shows, whole
};

form_state &form()
{
    static form_state state;
    return state;
}

float label_column(const std::vector<rv_editor_form_row_desc> &rows)
{
    float width = 0.0f;
    for (const auto &row : rows) {
        width = std::max(width, ImGui::CalcTextSize(row.label, rv_editor_label_end(row.label)).x);
    }
    return width + ImGui::GetStyle().ItemSpacing.x;
}

// The text a field has to show whole: the value, or the hint when it is empty (the longer of the two keeps the
// minimum stable between the states of a form).
float field_text_width(const rv_editor_form_row_desc &row)
{
    const float value = row.value != nullptr ? ImGui::CalcTextSize(row.value).x : 0.0f;
    const float hint = row.hint != nullptr ? ImGui::CalcTextSize(row.hint).x : 0.0f;
    return std::max(value, hint) + ImGui::GetStyle().FramePadding.x * 2.0f;
}

} // namespace

float rv_editor_form_min_width(const std::vector<rv_editor_form_row_desc> &rows)
{
    float field = 0.0f;
    for (const auto &row : rows) {
        field = std::max(field, field_text_width(row));
    }
    return label_column(rows) + field;
}

float rv_editor_form_min_height(const std::vector<rv_editor_form_row_desc> &rows)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    // Items: a field per row, an error line per can_fail row, the spacer and the button row.
    float height = ImGui::GetFrameHeight() * static_cast<float>(rows.size() + 1);
    float items = static_cast<float>(rows.size() + 2);
    for (const auto &row : rows) {
        if (row.can_fail) {
            height += ImGui::GetTextLineHeight();
            items += 1.0f;
        }
    }
    return height + style.ItemSpacing.y * (items - 1.0f);
}

void rv_editor_form_begin(const std::vector<rv_editor_form_row_desc> &rows, const rv_editor_theme &)
{
    form().start_x = ImGui::GetCursorPosX();
    form().field_x = form().start_x + label_column(rows);
    form().field_widths.clear();
    for (const auto &row : rows) {
        form().field_widths[row.label] = field_text_width(row);
    }
}

bool rv_editor_form_row(const char *label,
    char *buf,
    size_t size,
    const char *hint,
    bool can_fail,
    const char *error,
    const rv_editor_theme &theme,
    const rv_editor_field &field)
{
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, rv_editor_label_end(label));
    ImGui::SameLine(form().field_x);
    const float room = ImGui::GetContentRegionAvail().x;
    const float wanted =
        ImGui::CalcTextSize(buf[0] != '\0' ? buf : (hint != nullptr ? hint : "")).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(room, wanted));
    const std::string id = std::string("##") + label;
    const bool changed = rv_editor_text_field(id.c_str(), buf, size, theme, field);
    if (buf[0] == '\0' && hint != nullptr) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + pad.x, min.y + pad.y), rv_editor_col(theme.text_disabled), hint);
    }
    if (can_fail) {
        ImGui::SetCursorPosX(form().field_x);
        if (error != nullptr) {
            rv_editor_text_item(error, theme.error);
        } else {
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
        }
    }
    ImGui::PopID();
    return changed;
}

void rv_editor_form_custom_begin(const char *label, const rv_editor_theme &)
{
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, rv_editor_label_end(label));
    ImGui::SameLine(form().field_x);
    const auto it = form().field_widths.find(label);
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x, it == form().field_widths.end() ? 0.0f : it->second));
}

void rv_editor_form_custom_end(bool can_fail, const char *error, const rv_editor_theme &theme)
{
    if (can_fail) {
        ImGui::SetCursorPosX(form().field_x);
        if (error != nullptr) {
            rv_editor_text_item(error, theme.error);
        } else {
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
        }
    }
    ImGui::PopID();
}

float rv_editor_form_field_x()
{
    return form().field_x;
}

void rv_editor_form_buttons_begin(const rv_editor_theme &)
{
    ImGui::Spacing();
    ImGui::SetCursorPosX(form().start_x);
}

void rv_editor_form_end()
{
}

} // namespace rv_editor
