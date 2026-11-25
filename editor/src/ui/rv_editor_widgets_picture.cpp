#include "ui/rv_editor_widgets_picture.hpp"

#include <algorithm>
#include <cfloat>

#include "imgui_internal.h"
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

rv_editor_picture_rect rv_editor_picture_begin(const char *id, const rv_editor_theme &theme, bool clickable, bool *clicked)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const rv_editor_picture_rect area{ min, ImVec2(min.x + avail.x, min.y + avail.y) };
    if (clicked != nullptr) {
        *clicked = false;
    }
    if (avail.x < 1.0f || avail.y < 1.0f) {
        return area;
    }
    ImGui::GetWindowDrawList()->AddRectFilled(area.min, area.max, rv_editor_col(theme.code_base));
    if (clickable) {
        const bool hit = ImGui::InvisibleButton(id, avail);
        if (clicked != nullptr) {
            *clicked = hit;
        }
    } else {
        ImGui::Dummy(avail);
    }
    return area;
}

void rv_editor_picture_end()
{
}

rv_editor_picture_rect rv_editor_picture_frame(const rv_editor_picture_rect &area, int frame_w, int frame_h)
{
    if (frame_w <= 0 || frame_h <= 0) {
        return { area.min, area.min };
    }
    const float aw = area.max.x - area.min.x, ah = area.max.y - area.min.y;
    const float scale = std::min(aw / static_cast<float>(frame_w), ah / static_cast<float>(frame_h));
    const float w = static_cast<float>(frame_w) * scale, h = static_cast<float>(frame_h) * scale;
    const ImVec2 min(area.min.x + (aw - w) * 0.5f, area.min.y + (ah - h) * 0.5f);
    return { min, ImVec2(min.x + w, min.y + h) };
}

rv_editor_picture_rect rv_editor_picture_overlay(std::string_view text,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    const rv_editor_picture_rect &area)
{
    constexpr uint32_t backing_alpha = 0xb0; // the picture shows through, the text stays readable
    if (text.empty()) {
        return { area.min, area.min };
    }
    const float width = area.max.x - area.min.x;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const float h = ImGui::CalcTextSize(begin, end, false, width).y;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const rv_editor_picture_rect band{ area.min, ImVec2(area.max.x, area.min.y + h) };
    dl->AddRectFilled(band.min,
        band.max,
        (rv_editor_col(theme.code_base) & ~IM_COL32_A_MASK) | (backing_alpha << IM_COL32_A_SHIFT));
    dl->AddText(nullptr, 0.0f, area.min, rv_editor_col(rv_editor_label_colour(kind, theme)), begin, end, width);
    return band;
}

float rv_editor_picture_min_width(const std::vector<std::string> &texts, int max_lines)
{
    // Measured with the UI font explicitly: the minimums are asked before the first frame has a current font.
    ImFont *font = ImGui::GetIO().Fonts->Fonts[0];
    const float fs = font->LegacySize;
    float least = 0.0f;
    for (const std::string &text : texts) {
        const auto fits_lines = [&](float width) {
            return font->CalcTextSizeA(fs, FLT_MAX, width, text.c_str()).y <= static_cast<float>(max_lines) * fs + 0.01f;
        };
        float lo = 1.0f, hi = std::max(1.0f, font->CalcTextSizeA(fs, FLT_MAX, FLT_MAX, text.c_str()).x);
        while (hi - lo > 0.01f) {
            const float mid = 0.5f * (lo + hi);
            (fits_lines(mid) ? hi : lo) = mid;
        }
        least = std::max(least, hi);
    }
    return least;
}

} // namespace rv_editor
