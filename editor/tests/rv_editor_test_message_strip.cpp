// The message strip: drawn only with a text, under the content area at the bottom of the pane; nothing above it moves
// when it appears, the area loses height only at its bottom; buttons share the text's row; a long text wraps whole.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "panes/rv_editor_pane_tiled.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float width = 420.0f;
constexpr float height = 300.0f;
const std::vector<std::string> actions = { "Show in Output", "Dismiss" };
const std::string short_text = "Build #12 failed.";

std::string long_text()
{
    std::string s;
    while (s.size() < 300) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label.substr(0, e.label.find("##")) == label) {
            return &e;
        }
    }
    return nullptr;
}

struct rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
};

// The union of the entries recorded from index `from` on: the strip's rectangle whatever it is made of.
rect span(const rv_editor_probe_record &r, size_t from)
{
    rect u;
    for (size_t i = from; i < r.entries.size(); ++i) {
        const auto &e = r.entries[i];
        u = u.any ? rect{ std::min(u.x0, e.x0), std::min(u.y0, e.y0), std::max(u.x1, e.x1), std::max(u.y1, e.y1), true } :
                    rect{ e.x0, e.y0, e.x1, e.y1, true };
    }
    return u;
}

struct pane {
    rv_editor_probe_record record;
    float bottom = 0; // the content region's bottom edge
    float strip_h = 0;
    size_t strip_from = 0;
    int pressed = -1;
    ImGuiID area = 0; // the id of the area's frame item
};

// A shelf button, a content area with one row, and the strip under it; the way a pane lays itself out.
pane draw_pane(rv_editor_test_imgui &gui, const std::string &text, float w = width, float h = height)
{
    pane p;
    auto body = [&](rv_editor_probe_record &r) {
        p.bottom = ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y;
        ImGui::Button("shelf");
        r.item("shelf");
        p.strip_h = rv_editor_message_strip_height(text, actions, ImGui::GetContentRegionAvail().x);
        p.area = ImHashStr("##frame", 0, ImGui::GetID("##area"));
        (void)rv_editor_tiled_area_begin("##area", 100.0f, theme, {}, false, 0.0f, p.strip_h);
        rv_editor_list_row("##row", "first row", theme);
        r.item("row");
        rv_editor_tiled_area_end(theme);
        p.strip_from = r.entries.size();
        p.pressed = std::max(p.pressed, rv_editor_message_strip("strip", text, rv_editor_label_kind::error, actions, theme));
    };
    rv_editor_probe_draw(gui, w, body, h);
    p.pressed = -1;
    p.record = rv_editor_probe_draw(gui, w, body, h);
    return p;
}

const rv_editor_probe_entry *area_frame(const pane &p)
{
    for (const auto &e : p.record.entries) {
        if (e.id == p.area) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

// R1: no text, no strip: height 0, nothing drawn, no place kept.
RV_EDITOR_TEST(rv_editor_test_message_strip_with_no_text_is_not_there)
{
    rv_editor_test_imgui gui;
    const auto p = draw_pane(gui, "");
    RV_EDITOR_CHECK(p.strip_h == 0.0f);
    RV_EDITOR_CHECK(p.record.entries.size() == p.strip_from); // nothing was added after the area
    const auto *area = area_frame(p);
    RV_EDITOR_CHECK(area != nullptr);
    if (area != nullptr) {
        RV_EDITOR_CHECK(std::fabs(area->y1 - p.bottom) < 0.5f); // the area keeps all the height
    }
}

// R2: with a text the strip is a row at the very bottom of the pane, as high as the function says.
RV_EDITOR_TEST(rv_editor_test_message_strip_sits_at_the_bottom_of_the_pane)
{
    rv_editor_test_imgui gui;
    const auto p = draw_pane(gui, short_text);
    RV_EDITOR_CHECK(p.strip_h > 0.0f);
    const rect s = span(p.record, p.strip_from);
    RV_EDITOR_CHECK(s.any);
    RV_EDITOR_CHECK(std::fabs((s.y1 - s.y0) - p.strip_h) < 0.5f);
    RV_EDITOR_CHECK(std::fabs(s.y1 - p.bottom) < 0.5f);
}

// AC2: the shelf and the first row stay put; only the area's bottom edge rises, to the strip's top less the spacing.
RV_EDITOR_TEST(rv_editor_test_message_strip_moves_nothing_above_it)
{
    rv_editor_test_imgui gui;
    const auto without = draw_pane(gui, "");
    const auto with = draw_pane(gui, short_text);
    for (const char *label : { "shelf", "row" }) {
        const auto *a = find(without.record, label);
        const auto *b = find(with.record, label);
        RV_EDITOR_CHECK(a != nullptr && b != nullptr);
        if (a != nullptr && b != nullptr) {
            RV_EDITOR_CHECK(a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1);
        }
    }
    const auto *a = area_frame(without);
    const auto *b = area_frame(with);
    RV_EDITOR_CHECK(a != nullptr && b != nullptr);
    if (a != nullptr && b != nullptr) {
        RV_EDITOR_CHECK(a->y0 == b->y0 && a->x0 == b->x0 && a->x1 == b->x1);
        const rect s = span(with.record, with.strip_from);
        RV_EDITOR_CHECK(b->y1 < a->y1);
        RV_EDITOR_CHECK(b->y1 <= s.y0 + 0.5f);                                                // no overlap with the strip
        RV_EDITOR_CHECK(s.y0 - b->y1 <= ImGui::GetStyle().ItemSpacing.y + 0.5f);              // only the one gap between
        RV_EDITOR_CHECK(std::fabs((a->y1 - b->y1) - (with.strip_h + (s.y0 - b->y1))) < 0.5f); // the area gave up the strip
    }
}

// R2: the buttons stand in the strip's row, right of the text, inside the strip.
RV_EDITOR_TEST(rv_editor_test_message_strip_buttons_share_the_texts_row)
{
    rv_editor_test_imgui gui;
    const auto p = draw_pane(gui, short_text);
    const rect s = span(p.record, p.strip_from);
    for (const auto &name : actions) {
        const auto *b = find(p.record, name);
        RV_EDITOR_CHECK(b != nullptr);
        if (b != nullptr) {
            RV_EDITOR_CHECK(b->y0 >= s.y0 - 0.5f && b->y1 <= s.y1 + 0.5f);
            RV_EDITOR_CHECK(b->x1 <= s.x1 + 0.5f);
        }
    }
    // One line of text with buttons is one row high, not a text block over a button row.
    RV_EDITOR_CHECK(p.strip_h < 2.0f * ImGui::GetFrameHeight());
}

// The height follows the text up to three lines of it: a long text at a narrow width is capped there, and scrolls inside.
RV_EDITOR_TEST(rv_editor_test_message_strip_height_wraps_the_text)
{
    rv_editor_test_imgui gui;
    float short_h = 0, medium_h = 0, long_h = 0, narrow_h = 0, none_h = -1, three_h = 0;
    const std::string medium = "word0 word1 word2 word3 word4 word5 word6";
    gui.frame([&] {
        short_h = rv_editor_message_strip_height(short_text, actions, width);
        medium_h = rv_editor_message_strip_height(medium, actions, width);
        long_h = rv_editor_message_strip_height(long_text(), actions, width);
        narrow_h = rv_editor_message_strip_height(long_text(), actions, width / 2.0f);
        none_h = rv_editor_message_strip_height("", actions, width);
        three_h =
            std::max(ImGui::GetFrameHeight(), 3.0f * ImGui::GetTextLineHeight() + 2.0f * ImGui::GetStyle().FramePadding.y);
    });
    RV_EDITOR_CHECK(none_h == 0.0f);
    RV_EDITOR_CHECK(short_h > 0.0f);
    RV_EDITOR_CHECK(medium_h > short_h); // two lines: higher
    RV_EDITOR_CHECK(medium_h <= three_h + 0.5f);
    RV_EDITOR_CHECK(std::fabs(long_h - three_h) < 0.5f); // more than three lines: the cap
    RV_EDITOR_CHECK(std::fabs(narrow_h - three_h) < 0.5f);
}

// R19: a text of up to three lines is shown whole; a longer one is three lines high with a vertical scroll inside the strip
// (not cut with "..."), the buttons in the strip, and the strip fits the tile.
RV_EDITOR_TEST(rv_editor_test_message_strip_long_text_scrolls_inside_three_lines)
{
    rv_editor_test_imgui gui;
    const auto p = draw_pane(gui, long_text(), width, 400.0f);
    const rect s = span(p.record, p.strip_from);
    float three_h = 0.0f;
    gui.frame([&] {
        three_h =
            std::max(ImGui::GetFrameHeight(), 3.0f * ImGui::GetTextLineHeight() + 2.0f * ImGui::GetStyle().FramePadding.y);
    });
    RV_EDITOR_CHECK(s.any);
    RV_EDITOR_CHECK(std::fabs(p.strip_h - three_h) < 0.5f);
    RV_EDITOR_CHECK(std::fabs((s.y1 - s.y0) - p.strip_h) < 0.5f);
    for (const auto &name : actions) {
        const auto *b = find(p.record, name);
        RV_EDITOR_CHECK(b != nullptr && b->y0 >= s.y0 - 0.5f && b->y1 <= s.y1 + 0.5f && b->x1 <= s.x1 + 0.5f);
    }
    // A window inside the strip scrolls vertically: the text is longer than its room.
    bool scrolls = false;
    for (const ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        scrolls = scrolls || (w->ScrollMax.y > 0.0f && w->Pos.y >= s.y0 - 0.5f && w->Pos.y + w->Size.y <= s.y1 + 0.5f);
    }
    RV_EDITOR_CHECK(scrolls);
    RV_EDITOR_CHECK(rv_editor_fits_whole(p.record, ImRect(0, 0, width, 400.0f)).empty());
    // Up to three lines are whole, with no scroll anywhere in the strip.
    const auto q = draw_pane(gui, "word0 word1 word2 word3 word4 word5 word6", width, 400.0f);
    for (const ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        RV_EDITOR_CHECK(!(w->ScrollMax.y > 0.0f && w->Pos.y >= span(q.record, q.strip_from).y0 - 0.5f));
    }
}

// The strip returns the index of the button pressed, else -1; a disabled button is never returned.
RV_EDITOR_TEST(rv_editor_test_message_strip_returns_the_pressed_button)
{
    for (const bool disabled : { false, true }) {
        rv_editor_test_imgui gui;
        const std::vector<rv_editor_state> states = { {}, rv_editor_state{ rv_editor_look::live, "not now" } };
        int pressed = -1;
        auto body = [&](rv_editor_probe_record &) {
            const int k = rv_editor_message_strip("strip",
                short_text,
                rv_editor_label_kind::error,
                actions,
                theme,
                0.0f,
                disabled ? &states : nullptr);
            pressed = std::max(pressed, k);
        };
        rv_editor_probe_record r;
        for (int i = 0; i < 3; ++i) {
            r = rv_editor_probe_draw(gui, width, body, height);
        }
        RV_EDITOR_CHECK(pressed == -1); // nothing pressed yet
        const auto *button = find(r, "Dismiss");
        RV_EDITOR_CHECK(button != nullptr);
        if (button == nullptr) {
            continue;
        }
        ImGui::GetIO().AddMousePosEvent((button->x0 + button->x1) / 2, (button->y0 + button->y1) / 2);
        rv_editor_probe_draw(gui, width, body, height);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        rv_editor_probe_draw(gui, width, body, height);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        rv_editor_probe_draw(gui, width, body, height);
        RV_EDITOR_CHECK(pressed == (disabled ? -1 : 1));
    }
}
