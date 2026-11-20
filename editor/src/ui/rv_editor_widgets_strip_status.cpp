// The status line of a control strip: a window two text lines high that wraps its text whole.

#include "ui/rv_editor_widgets_strip_status.hpp"

#include <cmath>

#include "imgui.h"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

constexpr float status_lines = 2.0f;

// shortcut: a plain global, the editor draws from one thread; kept only for tests, drop it with them.
std::string &last_text()
{
    static std::string text;
    return text;
}

} // namespace

float rv_editor_strip_status_height()
{
    return status_lines * ImGui::GetTextLineHeight();
}

float rv_editor_strip_status_width_for(const std::string &text)
{
    const float room = rv_editor_strip_status_height() + 0.5f;
    const auto fits = [&](float w) {
        return ImGui::CalcTextSize(text.c_str(), nullptr, false, w).y <= room;
    };
    // A binary search over whole pixels finds a width that fits; wrapping at words is not monotonic, so a few pixels more are
    // checked and the least width from which none of them fails is taken.
    float lo = 1.0f;
    float hi = std::ceil(ImGui::CalcTextSize(text.c_str()).x) + 1.0f;
    while (lo < hi) {
        const float mid = std::floor((lo + hi) / 2.0f);
        if (fits(mid)) {
            hi = mid;
        } else {
            lo = mid + 1.0f;
        }
    }
    constexpr float probe = 64.0f;
    float least = lo;
    for (float w = lo; w < lo + probe; w += 1.0f) {
        if (!fits(w)) {
            least = w + 1.0f;
        }
    }
    return least;
}

void rv_editor_strip_status(const std::string &text, float width, const rv_editor_theme &theme, const std::string &tip)
{
    last_text() = text;
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_text));
    ImGui::BeginChild("##status",
        ImVec2(w, rv_editor_strip_status_height()),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    if (!text.empty()) {
        ImGui::TextWrapped("%s", text.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    const std::string &shown = tip.empty() ? text : tip;
    if (!shown.empty() && ImGui::IsItemHovered()) {
        rv_editor_tooltip(shown.c_str());
    }
}

const std::string &rv_editor_strip_status_last()
{
    return last_text();
}

} // namespace rv_editor
