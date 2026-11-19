// Long variable text wraps to its area and reads in full; a table cell that does not fit shows an ellipsis and its
// full text in a tooltip.

#include <string>

#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_table.hpp"

using namespace rv_editor;

namespace
{

constexpr float area_width = 200.0f;

std::string long_message()
{
    std::string s;
    while (s.size() < 300) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

// Draws `body` in a child `width` px wide; returns the last item's rect and the child's left edge.
template <class F>
ImRect draw_in_area(rv_editor_test::rv_editor_test_imgui &gui, float width, float *left, F body)
{
    ImRect rect;
    gui.frame([&] {
        ImGui::BeginChild("area", ImVec2(width, 0.0f), ImGuiChildFlags_None);
        *left = ImGui::GetCursorScreenPos().x;
        body();
        rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::EndChild();
    });
    return rect;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_wrap_status_wraps_inside_area)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    const std::string msg = long_message();
    float left = 0.0f;
    const ImRect r = draw_in_area(gui, area_width, &left, [&] {
        rv_editor_status(msg.c_str(), rv_editor_status_kind::error, theme);
    });
    RV_EDITOR_CHECK(r.GetHeight() > ImGui::GetFontSize() * 2.5f);
    RV_EDITOR_CHECK(r.Max.x <= left + area_width);
}

RV_EDITOR_TEST(rv_editor_test_wrap_status_short_stays_one_line)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    float left = 0.0f;
    const ImRect r = draw_in_area(gui, area_width, &left, [&] {
        rv_editor_status("ok", rv_editor_status_kind::ok, theme);
    });
    RV_EDITOR_CHECK(r.GetHeight() < ImGui::GetFontSize() * 2.0f);
}

RV_EDITOR_TEST(rv_editor_test_wrap_label_default_wraps)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    const std::string msg = long_message();
    float left = 0.0f;
    const ImRect r = draw_in_area(gui, area_width, &left, [&] {
        rv_editor_label(msg.c_str(), rv_editor_label_kind::error, theme);
    });
    RV_EDITOR_CHECK(r.GetHeight() > ImGui::GetFontSize() * 2.5f);
    RV_EDITOR_CHECK(r.Max.x <= left + area_width);
}

RV_EDITOR_TEST(rv_editor_test_wrap_label_line_stays_one_line)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    const std::string msg = long_message();
    float left = 0.0f;
    const ImRect r = draw_in_area(gui, area_width, &left, [&] {
        rv_editor_label(msg.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
    });
    RV_EDITOR_CHECK(r.GetHeight() < ImGui::GetFontSize() * 2.0f);
}

RV_EDITOR_TEST(rv_editor_test_wrap_ellipsize)
{
    rv_editor_test::rv_editor_test_imgui gui;
    gui.frame([&] {
        const std::string text = "a rather long cell text that cannot fit";
        const float full = ImGui::CalcTextSize(text.c_str()).x;
        // Fits: whole, no ellipsis.
        RV_EDITOR_CHECK(rv_editor_ellipsize(text.c_str(), full + 1.0f) == text);
        // Does not fit: shorter, ends in "...", fits the width, keeps the start.
        const std::string cut = rv_editor_ellipsize(text.c_str(), 80.0f);
        RV_EDITOR_CHECK(cut.size() < text.size());
        RV_EDITOR_CHECK(cut.size() > 3 && cut.substr(cut.size() - 3) == "...");
        RV_EDITOR_CHECK(ImGui::CalcTextSize(cut.c_str()).x <= 80.0f);
        RV_EDITOR_CHECK(cut.substr(0, 2) == "a ");
        // Nothing fits: still no overflow.
        RV_EDITOR_CHECK(ImGui::CalcTextSize(rv_editor_ellipsize(text.c_str(), 1.0f).c_str()).x <= 1.0f ||
            rv_editor_ellipsize(text.c_str(), 1.0f).empty());
    });
}

RV_EDITOR_TEST(rv_editor_test_wrap_cell_text_clips_and_tooltips)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    const std::string text = long_message();
    ImRect cell;
    auto draw = [&] {
        const rv_editor_table_column cols[2] = { { "a", rv_editor_table_width::fixed },
            { "b", rv_editor_table_width::stretch } };
        if (rv_editor_table_begin("t", cols, 2)) {
            rv_editor_table_row();
            rv_editor_table_cell();
            ImGui::Dummy(ImVec2(100.0f, 1.0f));
            rv_editor_table_cell();
            rv_editor_table_cell_text(text.c_str(), theme);
            cell = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            rv_editor_table_end();
        }
    };
    gui.frame(draw);
    RV_EDITOR_CHECK(cell.GetHeight() < ImGui::GetFontSize() * 2.0f);
    RV_EDITOR_CHECK(cell.Max.x <= 400.0f);
    // Hover it: the full text is in the tooltip window.
    ImGui::GetIO().MousePos = cell.GetCenter();
    gui.frame(draw);
    gui.frame(draw);
    const ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    RV_EDITOR_CHECK(tip != nullptr && tip->WasActive);
}

RV_EDITOR_TEST(rv_editor_test_wrap_text_area_flags)
{
    rv_editor_field field;
    RV_EDITOR_CHECK((rv_editor_text_area_flags(field) & ImGuiInputTextFlags_WordWrap) != 0);
    RV_EDITOR_CHECK((rv_editor_text_area_flags(field) & ImGuiInputTextFlags_ReadOnly) == 0);
    field.read_only = true;
    RV_EDITOR_CHECK((rv_editor_text_area_flags(field) & ImGuiInputTextFlags_WordWrap) != 0);
    RV_EDITOR_CHECK((rv_editor_text_area_flags(field) & ImGuiInputTextFlags_ReadOnly) != 0);
}

RV_EDITOR_TEST(rv_editor_test_wrap_open_project_row_narrow)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    rv_editor_app app;
    float left = 0.0f;
    float top = 0.0f;
    // The last item is the button: it sits on its own line below the wrapped message, inside the area.
    const ImRect button = draw_in_area(gui, 60.0f, &left, [&] {
        top = ImGui::GetCursorScreenPos().y;
        rv_editor_open_project_row(app, theme);
    });
    RV_EDITOR_CHECK(button.Min.y >= top + ImGui::GetTextLineHeight() * 1.5f);
    RV_EDITOR_CHECK(button.Min.x <= left + 1.0f);
}
