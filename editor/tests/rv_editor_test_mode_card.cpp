// S5: the big ask's card fills the tile content, keeps its title, scrolling text, message and buttons whole at the
// minimum, and its buttons stay where they are whatever the text and the message say.

#include <algorithm>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_mode.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;

const std::vector<std::string> three = { "Save", "Discard", "Keep Open" };

std::string long_text()
{
    std::string t;
    for (int i = 0; i < 40; ++i) {
        t += "Line " + std::to_string(i) + " says that unit_3.cpp has changes that were not saved yet. ";
    }
    return t;
}

rv_editor_mode_card_spec spec(const std::string &text, const std::string &message = "")
{
    rv_editor_mode_card_spec s;
    s.title = "Close unit_3.cpp?";
    s.text = text;
    s.message = message;
    s.buttons = three;
    return s;
}

struct drawn {
    rv_editor_probe_record record;
    ImVec2 min, avail; // the content region the card was opened in
    ImVec2 card_min;   // rv_editor_mode_card_min_size(three)
    int pressed = -1;
};

// Two settling frames in a window of w x h, then the one recorded. A zero minimum (no behaviour yet) falls back to a
// usable window, so the checks below fail on what is missing and not on an empty window.
drawn draw(rv_editor_test_imgui &gui, const rv_editor_mode_card_spec &s, float w, float h)
{
    drawn d;
    auto body = [&](rv_editor_probe_record &) {
        d.min = ImGui::GetCursorScreenPos();
        d.avail = ImGui::GetContentRegionAvail();
        d.card_min = rv_editor_mode_card_min_size(three, theme);
        const int p = rv_editor_mode_card_begin("card", s, theme);
        d.pressed = std::max(d.pressed, p);
        rv_editor_mode_card_end();
    };
    rv_editor_probe_draw(gui, w, body, h);
    d.pressed = -1;
    d.record = rv_editor_probe_draw(gui, w, body, h);
    return d;
}

// The window at the card's minimum (padding of the test window included), or a fallback when there is none.
ImVec2 window_at_min(rv_editor_test_imgui &gui)
{
    ImVec2 m(0, 0);
    gui.frame([&] {
        m = rv_editor_mode_card_min_size(three, theme);
        const ImVec2 pad = ImGui::GetStyle().WindowPadding;
        m = ImVec2(m.x + 2 * pad.x, m.y + 2 * pad.y);
    });
    return m.x > 2 * ImGui::GetStyle().WindowPadding.x && m.y > 2 * ImGui::GetStyle().WindowPadding.y ? m : ImVec2(200, 120);
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
    for (const auto &e : r.entries) {
        if (e.scroll_area) {
            return &e;
        }
    }
    return nullptr;
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

std::string buttons_moved(const drawn &a, const drawn &b)
{
    for (const auto &label : three) {
        const auto *ea = find(a.record, label);
        const auto *eb = find(b.record, label);
        if (ea == nullptr || eb == nullptr) {
            return "button '" + label + "' is not drawn";
        }
        if (ea->x0 != eb->x0 || ea->y0 != eb->y0 || ea->x1 != eb->x1 || ea->y1 != eb->y1) {
            return "button '" + label + "' moved";
        }
    }
    return "";
}

} // namespace

// The minimum: the title, 2 text lines, the message line and the button row high, as wide as the buttons together.
RV_EDITOR_TEST(rv_editor_test_mode_card_minimum_holds_title_two_lines_message_and_buttons)
{
    rv_editor_test_imgui gui;
    ImVec2 m(0, 0);
    float buttons_w = 0, line = 0, frame = 0, gap_y = 0, pad_y = 0;
    gui.frame([&] {
        m = rv_editor_mode_card_min_size(three, theme);
        for (const auto &b : three) {
            buttons_w += ImGui::CalcTextSize(b.c_str()).x;
        }
        buttons_w += ImGui::GetStyle().ItemSpacing.x * 2;
        line = ImGui::GetTextLineHeight();
        frame = ImGui::GetFrameHeight();
        gap_y = ImGui::GetStyle().ItemSpacing.y;
        pad_y = ImGui::GetStyle().WindowPadding.y;
    });
    RV_EDITOR_CHECK(m.x >= buttons_w);
    // title line + 2 text lines + message line + button row (a frame high)
    const float rows = 3 * line + frame + line;
    RV_EDITOR_CHECK(m.y >= rows);
    // and no more than those rows with the card's own padding and the gaps between five rows
    RV_EDITOR_CHECK(m.y <= rows + 2 * pad_y + 2 * 4 + 5 * gap_y + 2 * line);
}

RV_EDITOR_TEST(rv_editor_test_mode_card_minimum_follows_the_widest_button_row_only)
{
    rv_editor_test_imgui gui;
    ImVec2 two{}, three_b{}, four{}, same_h{};
    gui.frame([&] {
        two = rv_editor_mode_card_min_size({ "Yes", "No" }, theme);
        three_b = rv_editor_mode_card_min_size(three, theme);
        four = rv_editor_mode_card_min_size({ "Save", "Discard", "Keep Open", "Cancel the whole thing" }, theme);
        same_h = rv_editor_mode_card_min_size({ "A" }, theme);
    });
    RV_EDITOR_CHECK(two.x > 0);
    RV_EDITOR_CHECK(two.x < three_b.x);
    RV_EDITOR_CHECK(three_b.x < four.x);
    RV_EDITOR_CHECK(two.y == three_b.y && three_b.y == four.y && four.y == same_h.y);
}

// The card takes the whole content region of the tile.
RV_EDITOR_TEST(rv_editor_test_mode_card_fills_the_content_region)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    for (const auto &extra : { ImVec2(0, 0), ImVec2(120, 90) }) {
        const drawn d = draw(gui, spec("Save changes?"), w.x + extra.x, w.y + extra.y);
        const rv_editor_probe_entry *card = nullptr;
        for (const auto &e : d.record.entries) {
            if (e.x0 == d.min.x && e.y0 == d.min.y && e.x1 == d.min.x + d.avail.x && e.y1 == d.min.y + d.avail.y) {
                card = &e;
            }
        }
        RV_EDITOR_CHECK(card != nullptr);
    }
}

// fits_whole at the minimum with a short text, a long text and an error.
RV_EDITOR_TEST(rv_editor_test_mode_card_fits_whole_at_the_minimum_in_all_three_states)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    const std::vector<rv_editor_mode_card_spec> states = { spec("Save changes?"),
        spec(long_text()),
        spec("Save changes?", "The file is read only") };
    for (const auto &s : states) {
        const drawn d = draw(gui, s, w.x, w.y);
        const ImRect tile(0, 0, w.x, w.y);
        RV_EDITOR_CHECK(rv_editor_fits_whole(d.record, tile).empty());
        RV_EDITOR_CHECK(d.record.content_height <= w.y);
        for (const auto &label : three) {
            RV_EDITOR_CHECK(find(d.record, label) != nullptr);
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_mode_card_buttons_stay_in_place_across_text_and_error)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    for (const auto &size : { w, ImVec2(w.x + 120, w.y + 90) }) {
        const drawn base = draw(gui, spec("Save changes?"), size.x, size.y);
        const drawn lengthy = draw(gui, spec(long_text()), size.x, size.y);
        const drawn failed = draw(gui, spec("Save changes?", "The file is read only"), size.x, size.y);
        const drawn both = draw(gui, spec(long_text(), "The file is read only"), size.x, size.y);
        RV_EDITOR_CHECK(buttons_moved(base, lengthy).empty());
        RV_EDITOR_CHECK(buttons_moved(base, failed).empty());
        RV_EDITOR_CHECK(buttons_moved(base, both).empty());
    }
}

// One row: the buttons share their top and bottom, go left to right, and sit under the text area at the card's bottom.
RV_EDITOR_TEST(rv_editor_test_mode_card_buttons_make_one_row_at_the_bottom)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    for (const auto &size : { w, ImVec2(w.x + 120, w.y + 90) }) {
        const drawn d = draw(gui, spec("Save changes?"), size.x, size.y);
        const auto *save = find(d.record, "Save");
        const auto *discard = find(d.record, "Discard");
        const auto *keep = find(d.record, "Keep Open");
        const auto *area = text_area(d.record);
        RV_EDITOR_CHECK(save != nullptr && discard != nullptr && keep != nullptr && area != nullptr);
        if (save == nullptr || discard == nullptr || keep == nullptr || area == nullptr) {
            continue;
        }
        RV_EDITOR_CHECK(save->y0 == discard->y0 && discard->y0 == keep->y0);
        RV_EDITOR_CHECK(save->y1 == discard->y1 && discard->y1 == keep->y1);
        RV_EDITOR_CHECK(save->x1 <= discard->x0 && discard->x1 <= keep->x0);
        RV_EDITOR_CHECK(area->y1 <= save->y0);
        RV_EDITOR_CHECK(keep->y1 <= d.min.y + d.avail.y);
        // the row hugs the bottom: nothing but padding and the border under it
        RV_EDITOR_CHECK(d.min.y + d.avail.y - keep->y1 <= 2 * ImGui::GetStyle().WindowPadding.y + 8);
    }
}

// The text area keeps 2 lines at the minimum and grows with the tile.
RV_EDITOR_TEST(rv_editor_test_mode_card_text_area_has_two_lines_at_the_minimum_and_grows_with_the_tile)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    const drawn small = draw(gui, spec(long_text()), w.x, w.y);
    const drawn big = draw(gui, spec(long_text()), w.x, w.y + 90);
    const auto *a = text_area(small.record);
    const auto *b = text_area(big.record);
    RV_EDITOR_CHECK(a != nullptr && b != nullptr);
    if (a != nullptr && b != nullptr) {
        RV_EDITOR_CHECK(a->y1 - a->y0 >= 2 * ImGui::GetTextLineHeight());
        RV_EDITOR_CHECK(b->y1 - b->y0 >= a->y1 - a->y0 + 90 - 1);
    }
}

// The text scrolls inside its area, with a scroll bar only when it does not fit.
RV_EDITOR_TEST(rv_editor_test_mode_card_long_text_has_a_scroll_bar_and_short_text_has_none)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    const drawn lengthy = draw(gui, spec(long_text()), w.x, w.y);
    const auto *a = text_area(lengthy.record);
    RV_EDITOR_CHECK(a != nullptr);
    if (a != nullptr) {
        const ImGuiWindow *win = ImGui::FindWindowByID(a->id);
        RV_EDITOR_CHECK(win != nullptr && win->ContentSize.y > win->InnerRect.GetHeight());
    }
    const drawn brief = draw(gui, spec("Save changes?"), w.x, w.y);
    const auto *b = text_area(brief.record);
    RV_EDITOR_CHECK(b != nullptr);
    if (b != nullptr) {
        const ImGuiWindow *win = ImGui::FindWindowByID(b->id);
        RV_EDITOR_CHECK(win != nullptr && win->ContentSize.y <= win->InnerRect.GetHeight());
    }
    // the card itself never scrolls: its own window has no bar even with a long text
    for (const auto &e : lengthy.record.entries) {
        RV_EDITOR_CHECK(!(e.scrollbar && !e.scroll_area));
    }
}

// The text is on Catppuccin: the code base as the background, the code text colour for the text.
RV_EDITOR_TEST(rv_editor_test_mode_card_text_is_drawn_on_catppuccin)
{
    rv_editor_test_imgui gui(true);
    const ImVec2 w = window_at_min(gui);
    draw(gui, spec("Save changes?"), w.x, w.y);
    RV_EDITOR_CHECK(paints(theme.code_base));
    RV_EDITOR_CHECK(paints(theme.code_text));
}

// The message line is the place of an error: an error colours its text red and moves nothing.
RV_EDITOR_TEST(rv_editor_test_mode_card_error_message_is_drawn_in_the_error_colour)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    draw(gui, spec("Save changes?"), w.x, w.y);
    RV_EDITOR_CHECK(!paints(theme.error));
    draw(gui, spec("Save changes?", "The file is read only"), w.x, w.y);
    RV_EDITOR_CHECK(paints(theme.error));
}

// begin returns the index of the pressed button, else -1.
RV_EDITOR_TEST(rv_editor_test_mode_card_returns_the_pressed_button)
{
    rv_editor_test_imgui gui;
    const ImVec2 w = window_at_min(gui);
    const auto s = spec("Save changes?");
    const drawn idle = draw(gui, s, w.x, w.y);
    RV_EDITOR_CHECK(idle.pressed == -1);
    for (int i = 0; i < 3; ++i) {
        const auto *b = find(idle.record, three[i]);
        RV_EDITOR_CHECK(b != nullptr);
        if (b == nullptr) {
            continue;
        }
        int got = -1;
        auto body = [&](rv_editor_probe_record &) {
            got = std::max(got, rv_editor_mode_card_begin("card", s, theme));
            rv_editor_mode_card_end();
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
