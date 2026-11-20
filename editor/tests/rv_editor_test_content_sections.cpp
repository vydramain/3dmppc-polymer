// S7: a titled section inside one content area. The title is a band over the area's full inner width, the caller's
// controls (a field, buttons) sit on the same row at the right, the next item is below the row, and the minimum width
// holds the title and the controls together so the area needs no sideways scroll there.

#include <string>

#include "rv_editor_test_content.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::content;

namespace
{

constexpr float field_w = 90.0f;
constexpr float button_w = 50.0f;

float controls_width()
{
    return field_w + ImGui::GetStyle().ItemSpacing.x + button_w;
}

// What one section frame drew, in screen pixels.
struct seen {
    ImRect inner;
    ImRect title, field, button, below;
    float title_w = 0.0f;
    float min_w = 0.0f;
    bool has_title = false;
};

// A section "Notes" with a field and a button on its title row, then one list row, in an area `area_w` wide.
seen draw_section(rv_editor_test_imgui &gui, float area_w, rv_editor_probe_record *out = nullptr)
{
    seen s;
    const auto r = settled(gui, area_w + 40.0f, 300, [&](rv_editor_probe_record &) {
        s.min_w = rv_editor_content_section_min_width("Notes", controls_width());
        rv_editor_content_begin("c", ImVec2(area_w, 200), theme);
        s.inner = ImGui::GetCurrentWindow()->InnerRect;
        rv_editor_content_section_begin("Notes", controls_width(), theme);
        char buffer[16] = "";
        ImGui::SetNextItemWidth(field_w);
        ImGui::InputText("##field", buffer, sizeof buffer);
        s.field = last_item();
        ImGui::SameLine();
        ImGui::Button("Mark##b", ImVec2(button_w, 0));
        s.button = last_item();
        rv_editor_content_section_end(theme);
        rv_editor_list_row("row", "first row", theme);
        s.below = last_item();
        rv_editor_content_end(theme);
    });
    for (const auto &e : r.entries) {
        if (e.label.substr(0, e.label.find("##")) == "Notes") {
            s.title = ImRect(e.x0, e.y0, e.x1, e.y1);
            s.title_w = e.text_w;
            s.has_title = true;
        }
    }
    if (out != nullptr) {
        *out = r;
    }
    return s;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_section_title_spans_the_areas_full_inner_width)
{
    load_texts();
    rv_editor_test_imgui gui;
    const seen s = draw_section(gui, 320);
    RV_EDITOR_CHECK(s.has_title); // an item whose visible label is the title
    RV_EDITOR_CHECK(s.title_w > 0.0f);
    RV_EDITOR_CHECK(near(s.title.Min.x, s.inner.Min.x, 1.5f));
    RV_EDITOR_CHECK(near(s.title.Max.x, s.inner.Max.x, 1.5f));
}

RV_EDITOR_TEST(rv_editor_test_section_controls_share_the_title_row_at_the_right)
{
    load_texts();
    rv_editor_test_imgui gui;
    const seen s = draw_section(gui, 320);
    // Same row: the controls' vertical middle lies within the title band.
    const float field_mid = (s.field.Min.y + s.field.Max.y) / 2.0f;
    const float button_mid = (s.button.Min.y + s.button.Max.y) / 2.0f;
    RV_EDITOR_CHECK(field_mid >= s.title.Min.y && field_mid <= s.title.Max.y);
    RV_EDITOR_CHECK(button_mid >= s.title.Min.y && button_mid <= s.title.Max.y);
    // The group is flush right (inside the area's padding) and the title text ends before it begins.
    RV_EDITOR_CHECK(s.button.Max.x <= s.inner.Max.x + 0.5f);
    RV_EDITOR_CHECK(s.button.Max.x >= s.inner.Max.x - 3.0f * ImGui::GetStyle().ItemSpacing.x - 4.0f);
    RV_EDITOR_CHECK(s.title.Min.x + s.title_w <= s.field.Min.x);
    RV_EDITOR_CHECK(s.field.Max.x <= s.button.Min.x);
}

RV_EDITOR_TEST(rv_editor_test_section_end_puts_the_next_item_below_the_title_row)
{
    load_texts();
    rv_editor_test_imgui gui;
    const seen s = draw_section(gui, 320);
    const float row_bottom = std::max(s.title.Max.y, std::max(s.field.Max.y, s.button.Max.y));
    RV_EDITOR_CHECK(s.below.Min.y >= row_bottom);
    RV_EDITOR_CHECK(s.below.Min.y - row_bottom <= 2.0f * ImGui::GetStyle().ItemSpacing.y + 1.0f);
    RV_EDITOR_CHECK(s.below.Min.x <= s.title.Min.x + 1.0f + ImGui::GetStyle().WindowPadding.x); // back at the left
}

RV_EDITOR_TEST(rv_editor_test_section_min_width_is_the_title_a_gap_and_the_controls)
{
    load_texts();
    rv_editor_test_imgui gui;
    gui.frame([&] {
        const float title_w = ImGui::CalcTextSize("Notes").x;
        const float w = rv_editor_content_section_min_width("Notes", controls_width());
        RV_EDITOR_CHECK(w >= title_w + ImGui::GetStyle().ItemSpacing.x + controls_width());
        RV_EDITOR_CHECK(rv_editor_content_section_min_width("Notes", controls_width() + 40.0f) >= w + 40.0f - 0.5f);
        const float longer = rv_editor_content_section_min_width("Notes and other long title", controls_width());
        RV_EDITOR_CHECK(longer > w);
        RV_EDITOR_CHECK(rv_editor_content_section_min_width("Notes", 0.0f) >= title_w);
    });
}

// At the minimum the title is whole, the controls do not cover it and the area scrolls neither way.
RV_EDITOR_TEST(rv_editor_test_section_fits_whole_at_its_minimum_width)
{
    load_texts();
    rv_editor_test_imgui gui;
    // The area's chrome (frame, scrollbar room): outer width minus inner width, measured on a roomy frame.
    const seen roomy = draw_section(gui, 400);
    const float chrome = 400.0f - roomy.inner.GetWidth();
    RV_EDITOR_CHECK(roomy.min_w > 0.0f);
    rv_editor_probe_record r;
    const seen s = draw_section(gui, roomy.min_w + chrome, &r);
    RV_EDITOR_CHECK(s.has_title);
    RV_EDITOR_CHECK(!bars_of(r).h && !bars_of(r).v);
    RV_EDITOR_CHECK(!shows_ellipsis(r));
    RV_EDITOR_CHECK(s.title_w > 0.0f && s.title.Min.x + s.title_w <= s.field.Min.x);
    RV_EDITOR_CHECK(s.button.Max.x <= s.inner.Max.x + 0.5f);
    RV_EDITOR_CHECK(s.title.Max.x <= s.inner.Max.x + 0.5f);
}
