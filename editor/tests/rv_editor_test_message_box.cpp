// S4: the message box has one rectangle in all six states: 2 text lines on Catppuccin, a vertical bar only when the text
// is longer, and one button row aligned right whose place is kept without buttons.

#include <algorithm>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_message.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;

struct state {
    const char *name;
    std::string text;
    rv_editor_label_kind kind;
    std::vector<std::string> buttons;
    bool scrolls; // the text is longer than 2 lines at the minimum width
};

const std::vector<std::string> actions = { "Show in Output", "Dismiss" };
const std::vector<std::string> ask = { "Delete", "Keep" };

const std::vector<state> &states()
{
    static const std::vector<state> s = {
        { "nothing", "", rv_editor_label_kind::normal, {}, false },
        { "info", "Build #12 succeeded in 4.2 s.", rv_editor_label_kind::info, {}, false },
        { "error", "Build #12 failed: sources have error findings.", rv_editor_label_kind::error, actions, false },
        { "long error",
            "Build #12 failed: the compiler stopped at src/game/unit_3.cpp:12:4 after use of undeclared identifier "
            "'rv_scene_object_without_any_component_attached' and 19 more errors. The first error is selected below; "
            "Show in Console Output opens the full compiler log with every command line and its exit code.",
            rv_editor_label_kind::error,
            actions,
            true },
        { "short ask", "Delete 'unit_3.cpp'? It goes to the trash.", rv_editor_label_kind::normal, ask, false },
        { "long ask",
            "Delete the folder 'assets/textures'? It has 214 files. They go to the trash, and the scenes that use them "
            "(main.scene.toml, menu.scene.toml, level_02.scene.toml) show a missing-texture mark until you restore them "
            "from the trash.",
            rv_editor_label_kind::normal,
            ask,
            true },
    };
    return s;
}

std::vector<std::vector<std::string>> rows()
{
    return { actions, ask };
}

struct drawn {
    rv_editor_probe_record record;
    ImVec2 min, avail; // the content region the box was opened in
    float box_h = 0;   // the cursor's advance over the box, without the item spacing
    float expected_h = 0;
    int pressed = -1;
};

drawn draw(rv_editor_test_imgui &gui, const state &s, float w, float h)
{
    drawn d;
    auto body = [&](rv_editor_probe_record &) {
        d.min = ImGui::GetCursorScreenPos();
        d.avail = ImGui::GetContentRegionAvail();
        d.expected_h = rv_editor_message_box_height();
        const int p = rv_editor_message_box("box", s.text, s.kind, s.buttons, theme);
        d.box_h = ImGui::GetCursorScreenPos().y - d.min.y - ImGui::GetStyle().ItemSpacing.y;
        d.pressed = std::max(d.pressed, p);
    };
    rv_editor_probe_draw(gui, w, body, h);
    d.pressed = -1;
    d.record = rv_editor_probe_draw(gui, w, body, h);
    return d;
}

// The window at the box's minimum width and height (padding included); a fallback while there is no behaviour, so the
// checks fail on what is missing and not on an empty window.
ImVec2 window_at_min(rv_editor_test_imgui &gui)
{
    ImVec2 m(0, 0);
    gui.frame([&] {
        const ImVec2 pad = ImGui::GetStyle().WindowPadding;
        m = ImVec2(rv_editor_message_box_min_width(rows()) + 2 * pad.x, rv_editor_message_box_height() + 2 * pad.y);
    });
    const ImVec2 pad = ImGui::GetStyle().WindowPadding;
    return m.x > 2 * pad.x && m.y > 2 * pad.y ? m : ImVec2(420, 120);
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

const rv_editor_probe_entry *text_area(const rv_editor_probe_record &r)
{
    const rv_editor_probe_entry *found = nullptr;
    for (const auto &e : r.entries) {
        if (e.scroll_area) {
            if (found != nullptr) {
                return nullptr; // exactly one scroll area is the contract
            }
            found = &e;
        }
    }
    return found;
}

bool paints(uint32_t rgb)
{
    for (const ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        for (const ImDrawVert &v : w->DrawList->VtxBuffer) {
            if (v.col == rv_editor_col(rgb)) {
                return true;
            }
        }
    }
    return false;
}

bool equal(const rv_editor_probe_entry &a, const rv_editor_probe_entry &b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

} // namespace

// The minimum width: the widest button row plus 30 characters.
RV_EDITOR_TEST(rv_editor_test_message_box_min_width_is_the_widest_button_row_plus_30_characters)
{
    rv_editor_test_imgui gui;
    float m = 0, wide = 0, narrow = 0, row_w = 0, ch = 0, extra = 0;
    gui.frame([] {}); // the first frame builds the font
    gui.frame([&] {
        m = rv_editor_message_box_min_width(rows());
        narrow = rv_editor_message_box_min_width({ { "OK" } });
        wide = rv_editor_message_box_min_width({ { "OK" }, { "Save", "Discard", "Keep Open", "Cancel the whole thing" } });
        for (const auto &b : actions) {
            row_w += ImGui::CalcTextSize(b.c_str()).x + 2 * ImGui::GetStyle().FramePadding.x;
        }
        row_w += ImGui::GetStyle().ItemSpacing.x;
        ch = ImGui::CalcTextSize("0").x;
        extra = 4 * ImGui::GetStyle().WindowPadding.x + 3 * ImGui::GetStyle().ScrollbarSize;
    });
    RV_EDITOR_CHECK(ch > 0 && row_w > 0);
    RV_EDITOR_CHECK(m >= row_w + 30 * ch);
    RV_EDITOR_CHECK(m <= row_w + 30 * ch + extra);
    RV_EDITOR_CHECK(narrow < m && m < wide);
}

// One rectangle: the box's height, the text area and the button row's place do not depend on the state.
RV_EDITOR_TEST(rv_editor_test_message_box_has_the_same_rectangles_in_all_six_states)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    std::vector<drawn> all;
    for (const auto &s : states()) {
        all.push_back(draw(gui, s, w.x, w.y));
    }
    const auto *first = text_area(all[0].record);
    RV_EDITOR_CHECK(first != nullptr);
    for (const auto &d : all) {
        RV_EDITOR_CHECK(d.expected_h > 0);
        RV_EDITOR_CHECK(d.box_h == d.expected_h);
        RV_EDITOR_CHECK(d.box_h == all[0].box_h);
        const auto *a = text_area(d.record);
        RV_EDITOR_CHECK(a != nullptr);
        if (first != nullptr && a != nullptr) {
            RV_EDITOR_CHECK(equal(*first, *a));
        }
    }
}

// The button row is the same wherever a state has buttons, and its place is kept under the text without any.
RV_EDITOR_TEST(rv_editor_test_message_box_button_row_is_one_fixed_row_aligned_right)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    float row_y0 = -1, row_y1 = -1, right = -1;
    for (const auto &s : states()) {
        const drawn d = draw(gui, s, w.x, w.y);
        const auto *area = text_area(d.record);
        RV_EDITOR_CHECK(area != nullptr);
        if (s.buttons.empty()) {
            RV_EDITOR_CHECK(d.box_h >= (area != nullptr ? area->y1 - d.min.y : 1e9f) + ImGui::GetFrameHeight());
            continue;
        }
        const auto *first = find(d.record, s.buttons.front());
        const auto *last = find(d.record, s.buttons.back());
        RV_EDITOR_CHECK(first != nullptr && last != nullptr);
        if (first == nullptr || last == nullptr || area == nullptr) {
            continue;
        }
        RV_EDITOR_CHECK(area->y1 <= first->y0);
        RV_EDITOR_CHECK(first->y1 - first->y0 == ImGui::GetFrameHeight());
        for (const auto &b : s.buttons) {
            const auto *e = find(d.record, b);
            RV_EDITOR_CHECK(e != nullptr && e->y0 == first->y0 && e->y1 == first->y1);
        }
        RV_EDITOR_CHECK(first->x1 <= last->x0 || s.buttons.size() == 1);
        // aligned right: the last button ends at the right edge less the padding and the border
        RV_EDITOR_CHECK(d.min.x + d.avail.x - last->x1 <= ImGui::GetStyle().WindowPadding.x + 2);
        if (row_y0 < 0) {
            row_y0 = first->y0;
            row_y1 = first->y1;
            right = last->x1;
        }
        RV_EDITOR_CHECK(first->y0 == row_y0 && first->y1 == row_y1 && last->x1 == right);
        RV_EDITOR_CHECK(d.box_h >= first->y1 - d.min.y);
    }
    RV_EDITOR_CHECK(row_y0 >= 0);
}

// The text area shows exactly 2 lines: its inside is two lines high plus its own padding, so no half of a third line.
RV_EDITOR_TEST(rv_editor_test_message_box_text_area_is_exactly_two_lines_high)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    for (const auto &s : states()) {
        const drawn d = draw(gui, s, w.x, w.y);
        const auto *a = text_area(d.record);
        RV_EDITOR_CHECK(a != nullptr);
        if (a == nullptr) {
            continue;
        }
        const ImGuiWindow *win = ImGui::FindWindowByID(a->id);
        RV_EDITOR_CHECK(win != nullptr);
        if (win == nullptr) {
            continue;
        }
        const float inner = win->InnerRect.GetHeight() - 2 * win->WindowPadding.y;
        const float slack = 2 * win->WindowBorderSize + 1;
        RV_EDITOR_CHECK(inner >= 2 * ImGui::GetTextLineHeight() - slack);
        RV_EDITOR_CHECK(inner <= 2 * ImGui::GetTextLineHeight() + slack);
    }
}

// The text wraps at words inside the area: no horizontal scroll, the text is as wide as the area at most.
RV_EDITOR_TEST(rv_editor_test_message_box_text_wraps_at_words_without_horizontal_scroll)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    for (const auto &s : states()) {
        const drawn d = draw(gui, s, w.x, w.y);
        const auto *a = text_area(d.record);
        RV_EDITOR_CHECK(a != nullptr);
        if (a == nullptr || s.text.empty()) {
            continue;
        }
        const ImGuiWindow *win = ImGui::FindWindowByID(a->id);
        RV_EDITOR_CHECK(win != nullptr && win->ContentSize.x <= win->InnerRect.GetWidth());
        // wrapped: longer than one line's worth of width means more than one line of content
        const float one_line = ImGui::CalcTextSize(s.text.c_str()).x;
        if (win != nullptr && one_line > 2 * win->InnerRect.GetWidth()) {
            RV_EDITOR_CHECK(win->ContentSize.y >= 3 * ImGui::GetTextLineHeight());
        }
    }
}

// A vertical bar only for a text longer than 2 lines.
RV_EDITOR_TEST(rv_editor_test_message_box_has_a_vertical_bar_only_for_a_long_text)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    for (const auto &s : states()) {
        const drawn d = draw(gui, s, w.x, w.y);
        const auto *a = text_area(d.record);
        RV_EDITOR_CHECK(a != nullptr);
        if (a == nullptr) {
            continue;
        }
        const ImGuiWindow *win = ImGui::FindWindowByID(a->id);
        RV_EDITOR_CHECK(win != nullptr);
        if (win != nullptr) {
            const bool over = win->ContentSize.y > win->InnerRect.GetHeight();
            RV_EDITOR_CHECK(over == s.scrolls);
            RV_EDITOR_CHECK(a->scrollbar == s.scrolls);
        }
        // the box itself never scrolls
        for (const auto &e : d.record.entries) {
            RV_EDITOR_CHECK(!(e.scrollbar && !e.scroll_area));
        }
    }
}

// The text sits on Catppuccin: code base behind it, code text for an ask, severity colours for info and error.
RV_EDITOR_TEST(rv_editor_test_message_box_text_is_drawn_on_catppuccin)
{
    rv_editor_test_imgui gui(true);
    const ImVec2 w = window_at_min(gui);
    draw(gui, states()[4], w.x, w.y);
    RV_EDITOR_CHECK(paints(theme.code_base));
    RV_EDITOR_CHECK(paints(theme.code_text));
    RV_EDITOR_CHECK(!paints(theme.error));
    draw(gui, states()[2], w.x, w.y);
    RV_EDITOR_CHECK(paints(theme.error));
}

// fits_whole at the minimum width in all six states.
RV_EDITOR_TEST(rv_editor_test_message_box_fits_whole_at_the_minimum_in_all_six_states)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    for (const auto &s : states()) {
        const drawn d = draw(gui, s, w.x, w.y);
        const ImRect tile(0, 0, w.x, w.y);
        RV_EDITOR_CHECK(rv_editor_fits_whole(d.record, tile).empty());
        RV_EDITOR_CHECK(d.record.content_height <= w.y);
        for (const auto &b : s.buttons) {
            RV_EDITOR_CHECK(find(d.record, b) != nullptr);
        }
    }
}

// begin returns the index of the pressed button, else -1.
RV_EDITOR_TEST(rv_editor_test_message_box_returns_the_pressed_button)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    const state &s = states()[4];
    const drawn idle = draw(gui, s, w.x, w.y);
    RV_EDITOR_CHECK(idle.pressed == -1);
    for (int i = 0; i < 2; ++i) {
        const auto *b = find(idle.record, s.buttons[i]);
        RV_EDITOR_CHECK(b != nullptr);
        if (b == nullptr) {
            continue;
        }
        int got = -1;
        auto body = [&](rv_editor_probe_record &) {
            got = std::max(got, rv_editor_message_box("box", s.text, s.kind, s.buttons, theme));
        };
        ImGui::GetIO().AddMousePosEvent((b->x0 + b->x1) / 2, (b->y0 + b->y1) / 2);
        rv_editor_probe_draw(gui, w.x, body, w.y);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        rv_editor_probe_draw(gui, w.x, body, w.y);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        rv_editor_probe_draw(gui, w.x, body, w.y);
        RV_EDITOR_CHECK(got == i);
        ImGui::GetIO().AddMousePosEvent(-1000, -1000);
        rv_editor_probe_draw(gui, w.x, body, w.y);
    }
}
