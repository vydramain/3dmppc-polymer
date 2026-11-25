// The message box: a two-line Catppuccin text area over a right-aligned button row, one rectangle in every state.

#include "ui/rv_editor_widgets_message.hpp"

#include <algorithm>
#include <cmath>

#include "imgui_internal.h"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_item_hook.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Text lines the area shows whole.
constexpr float text_lines = 2.0f;
// Padding edges around the text (one per side).
constexpr float padding_sides = 2.0f;

// The text window of a box in a width; the caller owns the id stack.
void rv_editor_message_text_child(const std::string &text, rv_editor_label_kind kind, const rv_editor_theme &theme, float width)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rv_editor_col(theme.code_base));
    ImGui::PushStyleColor(ImGuiCol_Text,
        rv_editor_col(kind == rv_editor_label_kind::normal ? theme.code_text : rv_editor_label_colour(kind, theme)));
    ImGui::BeginChild("##text", ImVec2(width, rv_editor_message_text_height()), ImGuiChildFlags_AlwaysUseWindowPadding);
    rv_editor_item_scroll_area();
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

float rv_editor_message_row_width(const std::vector<std::string> &buttons)
{
    float w = 0.0f;
    for (const auto &b : buttons) {
        w += rv_editor_button_width(b.c_str());
    }
    return buttons.empty() ? 0.0f : w + static_cast<float>(buttons.size() - 1) * ImGui::GetStyle().ItemSpacing.x;
}

} // namespace

float rv_editor_message_text_height()
{
    return text_lines * ImGui::GetTextLineHeight() + padding_sides * ImGui::GetStyle().WindowPadding.y;
}

float rv_editor_message_strip_min_width(const std::vector<std::vector<std::string>> &button_rows,
    int field_chars,
    float text_chars)
{
    float widest = 0.0f;
    for (const auto &row : button_rows) {
        widest = std::max(widest, rv_editor_message_row_width(row));
    }
    // The text area's padding on both sides and its vertical bar come on top of the characters.
    const float text = widest + text_chars * ImGui::CalcTextSize("0").x + std::floor(ImGui::GetStyle().ScrollbarSize) +
        padding_sides * ImGui::GetStyle().WindowPadding.x;
    if (field_chars <= 0) {
        return text;
    }
    const float field =
        static_cast<float>(field_chars) * ImGui::CalcTextSize("0").x + padding_sides * ImGui::GetStyle().FramePadding.x;
    return std::max(text, widest + ImGui::GetStyle().ItemSpacing.x + field);
}

void rv_editor_message_text(const char *id,
    const std::string &text,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    float min_width)
{
    ImGui::PushID(id);
    rv_editor_message_text_child(text, kind, theme, std::max(ImGui::GetContentRegionAvail().x, min_width));
    ImGui::PopID();
}

// The buttons side by side from `x` on one row at `y`; the index of the pressed one, else -1.
static int rv_editor_message_buttons(float x,
    float y,
    const std::vector<std::string> &buttons,
    const rv_editor_theme &theme,
    const std::vector<rv_editor_state> *button_states,
    const char *button_tooltip)
{
    int pressed = -1;
    for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        const bool has_state = button_states != nullptr && static_cast<size_t>(i) < button_states->size();
        const rv_editor_state state = has_state ? (*button_states)[static_cast<size_t>(i)] : rv_editor_state{};
        if (rv_editor_button(buttons[i].c_str(), theme, state)) {
            pressed = i;
        }
        if (button_tooltip != nullptr && state.disabled == nullptr) {
            rv_editor_item_tooltip(button_tooltip);
        }
        x += rv_editor_button_width(buttons[i].c_str()) + ImGui::GetStyle().ItemSpacing.x;
    }
    return pressed;
}

// The name field from `x` on the row at `y`, `room` px wide with the gap to the buttons left out; 0 when Enter presses the
// first button, else -1.
static int rv_editor_message_field(const rv_editor_message_strip_field &field,
    float x,
    float y,
    float room,
    const std::vector<std::string> &buttons,
    const rv_editor_theme &theme,
    const std::vector<rv_editor_state> *button_states)
{
    const float gap = buttons.empty() ? 0.0f : ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::SetNextItemWidth(std::max(1.0f, room - gap));
    if (field.focus) {
        ImGui::SetKeyboardFocusHere();
    }
    rv_editor_text_field("##field", field.buf, field.size, theme);
    const ImVec2 at = ImGui::GetItemRectMin();
    if (field.buf[0] == '\0' && field.hint != nullptr && !ImGui::IsItemActive()) {
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + pad.x, at.y + pad.y), rv_editor_col(theme.text_disabled), field.hint);
    }
    // A single-line field lets go of the keyboard on Enter.
    const bool first_active = button_states == nullptr || button_states->empty() || (*button_states)[0].disabled == nullptr;
    const bool enter =
        !buttons.empty() && first_active && ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    return enter ? 0 : -1;
}

// The room the strip's text wraps in: the width less the padding and, with no second row, the buttons' row.
static float rv_editor_strip_text_width(const std::vector<std::string> &buttons, float width, bool second_row)
{
    const float gap = buttons.empty() || second_row ? 0.0f : ImGui::GetStyle().ItemSpacing.x;
    const float row = second_row ? 0.0f : rv_editor_message_row_width(buttons);
    return std::max(1.0f, width - 2.0f * ImGui::GetStyle().FramePadding.x - row - gap);
}

float rv_editor_message_strip_max_height(bool second_row, int max_lines)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float own = static_cast<float>(max_lines) * ImGui::GetTextLineHeight() + 2.0f * style.FramePadding.y;
    return second_row ? own + style.ItemSpacing.y + ImGui::GetFrameHeight() : std::max(ImGui::GetFrameHeight(), own);
}

float rv_editor_message_strip_height(const std::string &text,
    const std::vector<std::string> &buttons,
    float width,
    bool second_row,
    int max_lines)
{
    if (text.empty()) {
        return 0.0f;
    }
    const ImGuiStyle &style = ImGui::GetStyle();
    // At most max_lines lines: a longer text scrolls inside the strip.
    const float wrapped =
        std::min(ImGui::CalcTextSize(text.c_str(), nullptr, false, rv_editor_strip_text_width(buttons, width, second_row)).y,
            static_cast<float>(max_lines) * ImGui::GetTextLineHeight());
    const float own = wrapped + 2.0f * style.FramePadding.y;
    // A field or a lead item goes in a row of its own under the text, left of the buttons.
    return second_row ? own + style.ItemSpacing.y + ImGui::GetFrameHeight() : std::max(ImGui::GetFrameHeight(), own);
}

int rv_editor_message_strip(const char *id,
    const std::string &text,
    rv_editor_label_kind kind,
    const std::vector<std::string> &buttons,
    const rv_editor_theme &theme,
    float width,
    const std::vector<rv_editor_state> *button_states,
    const rv_editor_message_strip_field *field,
    const std::function<void(float)> &lead,
    const char *button_tooltip,
    int max_lines)
{
    if (text.empty()) {
        return -1;
    }
    const bool second_row = field != nullptr || static_cast<bool>(lead);
    ImGui::PushID(id);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float h = rv_editor_message_strip_height(text, buttons, w, second_row, max_lines);
    ImGui::GetWindowDrawList()->AddRectFilled(min, ImVec2(min.x + w, min.y + h), rv_editor_col(theme.code_base));
    ImGui::SetCursorScreenPos(ImVec2(min.x + pad.x, min.y + pad.y));
    ImGui::PushStyleColor(ImGuiCol_Text,
        rv_editor_col(kind == rv_editor_label_kind::normal ? theme.code_text : rv_editor_label_colour(kind, theme)));
    const float text_w = rv_editor_strip_text_width(buttons, w, second_row);
    const float text_h = std::min(ImGui::CalcTextSize(text.c_str(), nullptr, false, text_w).y,
        static_cast<float>(max_lines) * ImGui::GetTextLineHeight());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rv_editor_col(theme.code_base));
    ImGui::BeginChild("##text", ImVec2(text_w, text_h));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    // Only a text that scrolls is a scroll area: a short one is part of the strip.
    if (ImGui::GetScrollMaxY() > 0.0f || ImGui::GetCurrentWindow()->ScrollbarY) {
        rv_editor_item_scroll_area();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    const float row_y = second_row ? min.y + h - ImGui::GetFrameHeight() : min.y;
    const float buttons_x = min.x + w - pad.x - rv_editor_message_row_width(buttons);
    int pressed = -1;
    if (field != nullptr) {
        pressed =
            rv_editor_message_field(*field, min.x + pad.x, row_y, buttons_x - min.x - pad.x, buttons, theme, button_states);
    } else if (lead) {
        ImGui::SetCursorScreenPos(ImVec2(min.x + pad.x, row_y));
        lead(std::max(1.0f, buttons_x - min.x - pad.x - (buttons.empty() ? 0.0f : ImGui::GetStyle().ItemSpacing.x)));
    }
    if (const int hit = rv_editor_message_buttons(buttons_x, row_y, buttons, theme, button_states, button_tooltip); hit >= 0) {
        pressed = hit;
    }
    ImGui::SetCursorScreenPos(min);
    // One item of the whole strip, so its rectangle is seen whatever it is made of.
    const ImRect frame(min, ImVec2(min.x + w, min.y + h));
    ImGui::ItemSize(frame);
    ImGui::ItemAdd(frame, ImGui::GetID("##strip"), nullptr, ImGuiItemFlags_NoNav);
    ImGui::PopID();
    return pressed;
}

} // namespace rv_editor
