// Text fields, numeric spinners and dropdowns: ImGui edits the value, the
// editor draws the sunken well, the state frame and the markers.

#include <cmath>

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Divisor for vertical centering and half-height calculations.
constexpr float half_divisor = 2.0f;
// Number of ImGui style colors pushed by rv_editor_field_begin.
constexpr int field_style_colors = 3;
// Number of spinner arrow buttons (up and down).
constexpr int spinner_buttons = 2;
// UTF-8 byte mask for continuation byte detection.
constexpr unsigned char utf8_continuation_mask = 0xc0;
// UTF-8 continuation byte value for multi-byte sequences.
constexpr unsigned char utf8_continuation_byte = 0x80;
// Error marker glyph for field invalid state indicator.
constexpr const char *error_marker_glyph = "!";
// Dirty marker glyph for field unsaved state indicator.
constexpr const char *dirty_marker_glyph = "*";

struct rv_editor_field_look {
    bool hovered;
    bool focused;
};

rv_editor_field_look rv_editor_field_resolve(const rv_editor_field &f, bool hovered, bool focused)
{
    switch (f.state.look) {
    case rv_editor_look::live:
        return { hovered, focused };
    case rv_editor_look::normal:
        return { false, false };
    case rv_editor_look::hovered:
        return { true, false };
    case rv_editor_look::pressed:
    case rv_editor_look::focused:
        return { false, true };
    }
    return { hovered, focused };
}

// Well edge, then the one frame that says the most: invalid, focus, hover.
// An invalid field also gets a "!" so the error is not colour alone.
// The markers stay clear of `reserve` pixels at the right end (a dropdown's button).
void rv_editor_field_frame(ImDrawList *dl,
    ImVec2 min,
    ImVec2 max,
    const rv_editor_theme &t,
    const rv_editor_field &f,
    rv_editor_field_look look,
    float reserve)
{
    rv_editor_draw_bevel(dl, min, max, t, rv_editor_bevel::sunken);
    const bool disabled = f.state.disabled != nullptr;
    if (f.invalid != nullptr) {
        rv_editor_draw_frame(dl, min, max, t, t.error);
    } else if (!disabled && (look.focused || look.hovered)) {
        rv_editor_draw_frame(dl, min, max, t, t.selection);
    }

    const float pad = static_cast<float>(t.pad_px * t.scale);
    const float glyph = ImGui::CalcTextSize(error_marker_glyph).x;
    const float y = std::floor((min.y + max.y - ImGui::GetFontSize()) / half_divisor);
    float x = max.x - reserve - pad - glyph;
    if (f.invalid != nullptr) {
        dl->AddText(ImVec2(std::floor(x), y), rv_editor_col(t.error), error_marker_glyph);
        x -= glyph;
    }
    if (f.dirty) {
        dl->AddText(ImVec2(std::floor(x), y), rv_editor_col(t.warning), dirty_marker_glyph);
    }
}

// Opens the disabled scope and the well colour every field shares.
void rv_editor_field_begin(const rv_editor_theme &t, const rv_editor_field &f)
{
    if (f.state.disabled != nullptr) {
        ImGui::BeginDisabled();
    }
    const uint32_t well = f.read_only ? t.window : t.inset;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, rv_editor_col(well));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, rv_editor_col(well));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, rv_editor_col(well));
}

// Closes what rv_editor_field_begin opened and attaches the tooltip: why it is
// disabled, or else what is wrong with the value.
void rv_editor_field_end(const rv_editor_field &f)
{
    ImGui::PopStyleColor(field_style_colors);
    if (f.state.disabled != nullptr) {
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", f.state.disabled);
        return;
    }
    if (f.invalid != nullptr) {
        ImGui::SetItemTooltip("%s", f.invalid);
    }
}

} // namespace

bool rv_editor_text_field(const char *label, char *buf, size_t size, const rv_editor_theme &theme, const rv_editor_field &field)
{
    // Before the call: SetNextItemWidth applies to the next item only.
    const float width = ImGui::CalcItemWidth();
    rv_editor_field_begin(theme, field);
    const ImGuiInputTextFlags flags = field.read_only ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    const bool changed = ImGui::InputText(label, buf, size, flags);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max(min.x + width, ImGui::GetItemRectMax().y);
    const rv_editor_field_look look = rv_editor_field_resolve(field, ImGui::IsItemHovered(), ImGui::IsItemActive());
    rv_editor_field_end(field);

    rv_editor_field_frame(ImGui::GetWindowDrawList(), min, max, theme, field, look, 0.0f);
    return changed;
}

bool rv_editor_spinner(const char *label, int *value, int step, const rv_editor_theme &theme, const rv_editor_field &field)
{
    const float h = ImGui::GetFrameHeight();
    const float width = ImGui::CalcItemWidth();
    ImGui::PushID(label);

    rv_editor_field_begin(theme, field);
    ImGui::SetNextItemWidth(width - h);
    const ImGuiInputTextFlags flags = field.read_only ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    bool changed = ImGui::InputInt("##value", value, 0, 0, flags);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const rv_editor_field_look look = rv_editor_field_resolve(field, ImGui::IsItemHovered(), ImGui::IsItemActive());
    rv_editor_field_end(field);
    rv_editor_field_frame(ImGui::GetWindowDrawList(), min, max, theme, field, look, 0.0f);

    // Two half-height arrow buttons stacked to the right of the well.
    rv_editor_state arrows = field.state;
    arrows.look = rv_editor_look::live;
    if (field.read_only && arrows.disabled == nullptr) {
        arrows.disabled = rv_editor_text("widgets_fields.readonly_arrow");
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float half = std::floor(h / half_divisor);
    const ImVec2 origin(max.x, min.y);
    const ImGuiDir dirs[] = { ImGuiDir_Up, ImGuiDir_Down };
    for (int i = 0; i < spinner_buttons; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + half * static_cast<float>(i)));
        const rv_editor_item item = rv_editor_item_add(i == 0 ? "##up" : "##down", ImVec2(h, i == 0 ? half : h - half), arrows);
        const bool down = item.held && item.hovered;
        rv_editor_draw_panel(dl,
            item.min,
            item.max,
            theme,
            theme.button,
            down ? rv_editor_bevel::sunken : rv_editor_bevel::raised);
        rv_editor_draw_arrow(dl, item.min, item.max, theme, dirs[i], rv_editor_item_text(theme, item));
        if (item.clicked) {
            *value += i == 0 ? step : -step;
            changed = true;
        }
    }
    // One layout item over the whole spinner, so what follows starts below it.
    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(width, h));

    ImGui::PopID();
    return changed;
}

bool rv_editor_dropdown(const char *label,
    int *current,
    const char *const items[],
    int count,
    const rv_editor_theme &theme,
    const rv_editor_field &field)
{
    const float width = ImGui::CalcItemWidth();
    rv_editor_field_begin(theme, field);
    const char *preview = (*current >= 0 && *current < count) ? items[*current] : "";
    bool changed = false;
    const bool open = ImGui::BeginCombo(label, preview, ImGuiComboFlags_NoArrowButton);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max(min.x + width, ImGui::GetItemRectMax().y);
    const rv_editor_field_look look = rv_editor_field_resolve(field, ImGui::IsItemHovered(), open);
    // Before the list: the tooltip belongs to the combo, not to its last row.
    rv_editor_field_end(field);
    if (open) {
        for (int i = 0; i < count; ++i) {
            if (ImGui::Selectable(items[i], i == *current) && !field.read_only) {
                changed = *current != i;
                *current = i;
            }
        }
        ImGui::EndCombo();
    }

    // The well, then a raised arrow button inside its right end.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float bevel = static_cast<float>(theme.bevel_px * theme.scale);
    const float h = max.y - min.y;
    rv_editor_field_frame(dl, min, max, theme, field, look, h);
    const ImVec2 button_min(max.x - h + bevel, min.y + bevel);
    const ImVec2 button_max(max.x - bevel, max.y - bevel);
    rv_editor_draw_panel(dl, button_min, button_max, theme, theme.button, rv_editor_bevel::raised);
    rv_editor_draw_arrow(dl,
        button_min,
        button_max,
        theme,
        ImGuiDir_Down,
        field.state.disabled != nullptr ? theme.text_disabled : theme.text);
    return changed;
}

void rv_editor_path_row(const char *label, const std::string &path, const rv_editor_theme &theme)
{
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, rv_editor_label_end(label));
    ImGui::SameLine();
    const char *copy_label = rv_editor_text("widgets_fields.button_copy");
    const float room = ImGui::GetContentRegionAvail().x - rv_editor_button_width(copy_label) - ImGui::GetStyle().ItemSpacing.x;
    std::string shown = path;
    if (ImGui::CalcTextSize(path.c_str()).x > room) {
        const char *path_ellipsis = rv_editor_text("widgets_fields.path_ellipsis");
        // Drop whole UTF-8 characters from the front until "..." and the rest fit.
        size_t start = 0;
        do {
            ++start;
            while (start < path.size()) {
                const auto byte = static_cast<unsigned char>(path[start]);
                if ((byte & utf8_continuation_mask) != utf8_continuation_byte) {
                    break;
                }
                ++start;
            }
            shown = std::string(path_ellipsis) + path.substr(start);
        } while (start < path.size() && ImGui::CalcTextSize(shown.c_str()).x > room);
    }
    ImGui::TextUnformatted(shown.c_str());
    ImGui::SetItemTooltip("%s", path.c_str());
    ImGui::SameLine();
    if (rv_editor_button(copy_label, theme)) {
        ImGui::SetClipboardText(path.c_str());
    }
    ImGui::PopID();
}

} // namespace rv_editor
