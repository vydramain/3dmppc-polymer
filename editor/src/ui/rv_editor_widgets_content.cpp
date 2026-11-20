// The content area: a code-background well around a Motif scroll area, and the two-line record row.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui_internal.h" // the scroll child's inner rect
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_item_hook.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"

namespace rv_editor
{

namespace
{

// Number of bevel edges of the frame around the scroll area (one per side).
constexpr float frame_sides = 2.0f;
// Rows the shortest content area shows whole.
constexpr float min_rows = 3.0f;
// The pill sits this many frame paddings from the area's edge.
constexpr float pill_margin_paddings = 1.0f;
constexpr int style_colours_pushed = 2;

struct rv_editor_content_frame {
    ImVec2 min; // outer rect, frame included
    ImVec2 max;
};

std::vector<rv_editor_content_frame> rv_editor_content_stack;

float rv_editor_content_frame_px(const rv_editor_theme &theme)
{
    return static_cast<float>(theme.pad_px * theme.scale);
}

// Mixed public and private Selectable flags: the int cast avoids -Wdeprecated-enum-enum-conversion.
constexpr ImGuiSelectableFlags inert_row_flags = static_cast<ImGuiSelectableFlags>(
    static_cast<int>(ImGuiSelectableFlags_Disabled) | static_cast<int>(ImGuiSelectableFlags_NoPadWithHalfSpacing));

// Where an open section's title row starts and how high it is.
struct rv_editor_section_row {
    ImVec2 pos;
    float height;
};

std::vector<rv_editor_section_row> rv_editor_section_rows;

} // namespace

float rv_editor_content_min_height()
{
    // No theme here: the style's FramePadding is the theme's pad, which is also the frame's width.
    const float pad = ImGui::GetStyle().FramePadding.y;
    // The rows, the gap above the first one, the horizontal bar and the frame.
    return min_rows * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y +
        std::floor(ImGui::GetStyle().ScrollbarSize) + frame_sides * pad;
}

bool rv_editor_content_begin(const char *id, ImVec2 size, const rv_editor_theme &theme)
{
    ImGui::PushID(id);
    const float pad = rv_editor_content_frame_px(theme);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 max(min.x + (size.x > 0.0f ? size.x : avail.x + size.x), min.y + (size.y > 0.0f ? size.y : avail.y + size.y));
    rv_editor_draw_panel(ImGui::GetWindowDrawList(), min, max, theme, theme.code_base, rv_editor_bevel::sunken);
    rv_editor_content_stack.push_back({ min, max });
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rv_editor_col(theme.code_base));
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_text));
    ImGui::SetCursorScreenPos(ImVec2(min.x + pad, min.y + pad));
    // An outer window of the area's own size, so the area keeps its rectangle whether its bars show or not.
    ImGui::BeginChild("##area",
        ImVec2(std::max(1.0f, max.x - min.x - frame_sides * pad), std::max(1.0f, max.y - min.y - frame_sides * pad)),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    rv_editor_item_scroll_area();
    const bool open = rv_editor_scroll_begin("##scroll", ImVec2(0.0f, 0.0f), true);
    // ImGui sizes a window's work rect by its content only with its own scrollbar; here rows and tables span the whole
    // content as of the last frame.
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    window->WorkRect.Max.x = std::max(window->WorkRect.Max.x, window->WorkRect.Min.x + rv_editor_scroll_content_width());
    ImGui::Dummy(ImVec2(0.0f, 0.0f)); // a gap above the first row, which a Selectable's box reaches into
    return open;
}

void rv_editor_content_end(const rv_editor_theme &theme)
{
    const rv_editor_content_frame f = rv_editor_content_stack.back();
    rv_editor_content_stack.pop_back();
    rv_editor_scroll_end(theme);
    ImGui::EndChild();
    ImGui::PopStyleColor(style_colours_pushed);
    // The whole area as one item, frame included, as after a well.
    ImGui::SetCursorScreenPos(f.min);
    ImGui::Dummy(ImVec2(f.max.x - f.min.x, f.max.y - f.min.y));
    ImGui::PopID();
}

void rv_editor_content_empty_row(const char *text, const rv_editor_theme &theme)
{
    // A disabled Selectable, not a Text: the row is an item with an id, like the rows it stands for.
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, rv_editor_col(theme.code_subtext));
    ImGui::Selectable((std::string(text) + "###empty").c_str(), false, inert_row_flags);
    ImGui::PopStyleColor();
}

float rv_editor_record_status_width(const char *const words[], int count)
{
    // Marks differ by kind; the widest one counts. The colours do not matter for a width.
    const rv_editor_theme theme = rv_editor_theme_olive;
    float widest = 0.0f;
    for (int i = 0; i < count; ++i) {
        for (const auto kind : { rv_editor_status_kind::ok, rv_editor_status_kind::warning, rv_editor_status_kind::error }) {
            widest = std::max(widest, rv_editor_status_width(words[i], kind, theme));
        }
    }
    return widest;
}

rv_editor_record_result rv_editor_record_row(const char *id,
    const char *name,
    const char *path,
    const char *status,
    rv_editor_status_kind kind,
    const rv_editor_theme &theme,
    const rv_editor_record_style &style)
{
    const ImGuiStyle &gs = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float own_w = rv_editor_status_width(status, kind, theme);
    const float status_w = std::max(style.status_width, own_w);
    // Two lines and the spacing between them, with no padding of its own: the row is as high as its text.
    const float row_h = 2.0f * ImGui::GetTextLineHeight() + gs.ItemSpacing.y;
    const float margin = gs.FramePadding.x * pill_margin_paddings;
    const float text_w = std::max(ImGui::CalcTextSize(name).x, ImGui::CalcTextSize(path).x);
    const float need = text_w + gs.ItemSpacing.x + status_w + margin;
    const ImGuiWindow *window = ImGui::GetCurrentWindow();

    rv_editor_record_result result;
    ImGui::PushID(id);
    result.clicked = ImGui::Selectable("##row", style.selected, ImGuiSelectableFlags_NoPadWithHalfSpacing, ImVec2(0.0f, row_h));
    result.double_clicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    result.right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    result.row_min = ImGui::GetItemRectMin();
    result.row_max = ImGui::GetItemRectMax();
    // The row's full width is reserved with a zero-size item, so the status never covers the end of a path.
    const float natural = result.row_max.x - status_w - margin;
    const float pill_x = std::min(natural, window->InnerRect.Max.x - status_w - margin);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + need, pos.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::SetCursorScreenPos(ImVec2(pill_x + status_w - own_w, pos.y));
    rv_editor_status(status, kind, theme);
    result.status_min = ImVec2(pill_x, pos.y);
    result.status_max = ImVec2(pill_x + status_w, pos.y + ImGui::GetTextLineHeight());
    ImGui::SetCursorScreenPos(pos);
    ImGui::TextUnformatted(name);
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_subtext));
    ImGui::TextUnformatted(path);
    ImGui::PopStyleColor();
    // The row's parts as items of the layout; a text under the pill is cut at it, as the eye reads it.
    const float line = ImGui::GetTextLineHeight();
    const float next = pos.y + line + gs.ItemSpacing.y;
    const float limit = std::max(pos.x + 1.0f, pill_x - gs.ItemSpacing.x);
    const auto part = [&](const char *key, float x0, float y0, float x1) {
        ImGui::ItemAdd(ImRect(x0, y0, x1, y0 + line), ImGui::GetID(key), nullptr, ImGuiItemFlags_NoNav);
    };
    part("##name", pos.x, pos.y, std::min(pos.x + ImGui::CalcTextSize(name).x, limit));
    part("##path", pos.x, next, std::min(pos.x + ImGui::CalcTextSize(path).x, limit));
    part("##status", pill_x, pos.y, pill_x + status_w);
    ImGui::PopID();
    return result;
}

float rv_editor_content_section_min_width(const char *title, float controls_width)
{
    return ImGui::CalcTextSize(title).x + ImGui::GetStyle().ItemSpacing.x + controls_width;
}

void rv_editor_content_section_begin(const char *title, float controls_width, const rv_editor_theme &theme)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    rv_editor_section_rows.push_back({ pos, height });
    // The title is a disabled Selectable over the full width, centred in a row as high as the controls.
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, rv_editor_col(theme.code_text));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    // The title is its own id, so the sections of one area do not share one.
    ImGui::Selectable(title, false, inert_row_flags, ImVec2(0.0f, height));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCurrentWindow()->InnerRect.Max.x - controls_width, pos.y));
}

void rv_editor_content_section_end(const rv_editor_theme &)
{
    const rv_editor_section_row row = rv_editor_section_rows.back();
    rv_editor_section_rows.pop_back();
    ImGui::SetCursorScreenPos(ImVec2(row.pos.x, row.pos.y + row.height + ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

} // namespace rv_editor
