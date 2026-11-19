// The shared message line (a constant place for an error/warning/note line): nothing after it appears, moves or
// resizes when the text changes.

#include <string>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr float width = 300.0f;
constexpr int line_count = 2;

std::string long_text()
{
    std::string s;
    while (s.size() < 400) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const char *label)
{
    for (const auto &e : r.entries) {
        if (e.label == label) {
            return &e;
        }
    }
    return nullptr;
}

// A message line and a button after it.
void line_body(rv_editor_probe_record &p, const std::string &text, int lines)
{
    rv_editor_message_line("msg", text.c_str(), lines, 0.0f, rv_editor_theme_olive);
    ImGui::Button("after");
    p.item("after");
}

// "" when the two records agree on the "after" item's rect and on the content height.
std::string after_difference(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    const auto *ea = find(a, "after");
    const auto *eb = find(b, "after");
    if (ea == nullptr || eb == nullptr) {
        return "no 'after' item";
    }
    if (ea->x0 != eb->x0 || ea->y0 != eb->y0 || ea->x1 != eb->x1 || ea->y1 != eb->y1) {
        return "'after' moved";
    }
    return a.content_height == b.content_height ? "" : "content height changed";
}

// Frames with the mouse over the centre of the message line (the first frame's rect; the first frame's mouse is
// (10, 5), above the line, since the rect is not known yet), so the hover settles; true when a tooltip is shown.
bool tooltip_over_line(const std::string &text)
{
    rv_editor_test_imgui gui;
    ImVec2 mouse(10.0f, 5.0f);
    for (int i = 0; i < 3; ++i) {
        ImGui::GetIO().MousePos = mouse;
        rv_editor_probe_draw(gui, width, [&](auto &) {
            rv_editor_message_line("msg", text.c_str(), line_count, 0.0f, rv_editor_theme_olive);
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            mouse = ImVec2((min.x + max.x) / 2.0f, (min.y + max.y) / 2.0f);
        });
    }
    ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    return tip != nullptr && tip->WasActive;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_message_line_same_rect_empty_short_long)
{
    rv_editor_test_imgui gui;
    const auto empty = rv_editor_probe_draw(gui, width, [](auto &p) {
        line_body(p, "", line_count);
    });
    const auto shorter = rv_editor_probe_draw(gui, width, [](auto &p) {
        line_body(p, "Build failed", line_count);
    });
    const auto longer = rv_editor_probe_draw(gui, width, [](auto &p) {
        line_body(p, long_text(), line_count);
    });
    RV_EDITOR_CHECK(rv_editor_probe_compare(empty, shorter).empty());
    RV_EDITOR_CHECK(rv_editor_probe_compare(empty, longer).empty());
}

RV_EDITOR_TEST(rv_editor_test_message_line_reserves_the_lines_when_empty)
{
    rv_editor_test_imgui gui;
    const auto r = rv_editor_probe_draw(gui, width, [](auto &p) {
        line_body(p, "", line_count);
    });
    const auto *after = find(r, "after");
    RV_EDITOR_CHECK(after != nullptr);
    if (after != nullptr) {
        RV_EDITOR_CHECK(after->y0 >= line_count * ImGui::GetTextLineHeight());
    }
}

RV_EDITOR_TEST(rv_editor_test_message_line_height_follows_the_line_count)
{
    rv_editor_test_imgui gui;
    const auto one = rv_editor_probe_draw(gui, width, [](auto &p) {
        line_body(p, "x", 1);
    });
    const auto three = rv_editor_probe_draw(gui, width, [](auto &p) {
        line_body(p, "x", 3);
    });
    const auto *a = find(one, "after");
    const auto *b = find(three, "after");
    RV_EDITOR_CHECK(a != nullptr && b != nullptr);
    if (a != nullptr && b != nullptr) {
        RV_EDITOR_CHECK(b->y0 - a->y0 >= 2.0f * ImGui::GetTextLineHeight());
    }
}

RV_EDITOR_TEST(rv_editor_test_message_line_survives_a_resize)
{
    rv_editor_test_imgui gui;
    const std::string text = long_text();
    const auto r = rv_editor_probe_resize(gui, width, width / 2.0f, [&](auto &p) {
        line_body(p, text, line_count);
    });
    RV_EDITOR_CHECK(r.difference.empty());
}

RV_EDITOR_TEST(rv_editor_test_message_line_long_text_has_a_tooltip)
{
    RV_EDITOR_CHECK(tooltip_over_line(long_text()));
}

RV_EDITOR_TEST(rv_editor_test_message_line_fitting_text_has_no_tooltip)
{
    RV_EDITOR_CHECK(!tooltip_over_line("Build failed"));
    RV_EDITOR_CHECK(!tooltip_over_line(""));
}
