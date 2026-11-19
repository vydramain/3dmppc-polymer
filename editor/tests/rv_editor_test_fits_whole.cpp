// The fits_whole check and the pane-states helper catch what they should and let scroll areas alone.

#include <cstdio>
#include <string>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
const ImRect tile(0, 0, 200, 100);

rv_editor_probe_entry entry(float x0, float y0, float x1, float y1, const char *label = "item")
{
    rv_editor_probe_entry e;
    e.id = 1;
    e.label = label;
    e.x0 = x0;
    e.y0 = y0;
    e.x1 = x1;
    e.y1 = y1;
    return e;
}

rv_editor_probe_record one(const rv_editor_probe_entry &e)
{
    rv_editor_probe_record r;
    r.entries.push_back(e);
    return r;
}

// Two settling frames, so what depends on the previous frame has settled.
template <class F>
rv_editor_probe_record drawn(rv_editor_test_imgui &gui, F body)
{
    rv_editor_probe_draw(
        gui,
        200,
        [&](auto &) {
            body();
        },
        100);
    return rv_editor_probe_draw(
        gui,
        200,
        [&](auto &) {
            body();
        },
        100);
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_fits_whole_accepts_an_item_inside_the_tile)
{
    RV_EDITOR_CHECK(rv_editor_fits_whole(one(entry(10, 10, 190, 90)), tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_fits_whole_catches_an_item_past_the_tile_edge)
{
    RV_EDITOR_CHECK(!rv_editor_fits_whole(one(entry(10, 10, 201, 90)), tile).empty());
    RV_EDITOR_CHECK(!rv_editor_fits_whole(one(entry(10, 10, 190, 101)), tile).empty());
    RV_EDITOR_CHECK(!rv_editor_fits_whole(one(entry(-1, 10, 190, 90)), tile).empty());
    RV_EDITOR_CHECK(!rv_editor_fits_whole(one(entry(10, -1, 190, 90)), tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_fits_whole_catches_a_text_wider_or_higher_than_its_rectangle)
{
    rv_editor_probe_entry e = entry(10, 10, 60, 30);
    e.text_w = 50;
    e.text_h = 20;
    RV_EDITOR_CHECK(rv_editor_fits_whole(one(e), tile).empty());
    e.text_w = 51;
    RV_EDITOR_CHECK(!rv_editor_fits_whole(one(e), tile).empty());
    e.text_w = 50;
    e.text_h = 21;
    RV_EDITOR_CHECK(!rv_editor_fits_whole(one(e), tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_fits_whole_catches_an_added_ellipsis)
{
    RV_EDITOR_CHECK(!rv_editor_fits_whole(one(entry(10, 10, 190, 30, "long_na...")), tile).empty());
    RV_EDITOR_CHECK(rv_editor_fits_whole(one(entry(10, 10, 190, 30, "long_name")), tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_fits_whole_catches_a_scroll_bar_on_a_window_that_is_not_a_scroll_area)
{
    rv_editor_probe_entry e = entry(10, 10, 190, 90, "window");
    e.scrollbar = true;
    RV_EDITOR_CHECK(!rv_editor_fits_whole(one(e), tile).empty());
    e.scroll_area = true; // a scroll area shows its bar when its content is larger
    RV_EDITOR_CHECK(rv_editor_fits_whole(one(e), tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_fits_whole_ignores_an_item_marked_inside_a_scroll_area)
{
    rv_editor_probe_entry e = entry(10, 10, 400, 90);
    e.in_scroll = true;
    e.text_w = 500;
    RV_EDITOR_CHECK(rv_editor_fits_whole(one(e), tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_probe_measures_the_text_of_a_label_without_its_id_part)
{
    rv_editor_test_imgui gui;
    const float hi = [&] {
        float w = 0;
        gui.frame([&] {
            w = ImGui::CalcTextSize("Hi").x;
        });
        return w;
    }();
    const rv_editor_probe_record r = drawn(gui, [] {
        ImGui::Button("Hi##a_much_longer_identifier", ImVec2(60, 30));
    });
    bool found = false;
    for (const auto &e : r.entries) {
        if (e.label.rfind("Hi", 0) == 0) {
            found = true;
            RV_EDITOR_CHECK(e.text_w == hi);
            RV_EDITOR_CHECK(e.text_h > 0.0f);
        }
    }
    RV_EDITOR_CHECK(found);
    RV_EDITOR_CHECK(rv_editor_fits_whole(r, tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_fits_whole_catches_a_drawn_button_past_the_edge_or_narrower_than_its_text)
{
    rv_editor_test_imgui gui;
    const auto past = drawn(gui, [] {
        ImGui::SetCursorPos(ImVec2(150, 10));
        ImGui::Button("Edge", ImVec2(100, 20));
    });
    RV_EDITOR_CHECK(!rv_editor_fits_whole(past, tile).empty());
    const auto narrow = drawn(gui, [] {
        ImGui::Button("A label wider than the button", ImVec2(30, 20));
    });
    RV_EDITOR_CHECK(!rv_editor_fits_whole(narrow, tile).empty());
    const auto low = drawn(gui, [] {
        ImGui::Button("Low", ImVec2(60, 4));
    });
    RV_EDITOR_CHECK(!rv_editor_fits_whole(low, tile).empty());
    const auto ellipsis = drawn(gui, [] {
        ImGui::Button("Name...", ImVec2(90, 20));
    });
    RV_EDITOR_CHECK(!rv_editor_fits_whole(ellipsis, tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_fits_whole_ignores_a_wide_button_inside_a_scroll_area)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    const auto inside = drawn(gui, [] {
        if (rv_editor_scroll_begin("area", ImVec2(150, 60), true)) {
            ImGui::Button("Wide", ImVec2(400, 20));
            ImGui::Dummy(ImVec2(400, 200)); // content larger than the area: its own bars show
        }
        rv_editor_scroll_end(theme);
    });
    bool marked = false;
    for (const auto &e : inside.entries) {
        marked = marked || (e.in_scroll && e.x1 > tile.Max.x);
    }
    RV_EDITOR_CHECK(marked);
    RV_EDITOR_CHECK(rv_editor_fits_whole(inside, tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_fits_whole_catches_a_plain_child_with_a_scroll_bar)
{
    rv_editor_test_imgui gui;
    const auto plain = drawn(gui, [] {
        if (ImGui::BeginChild("plain", ImVec2(150, 60))) {
            ImGui::Dummy(ImVec2(100, 400));
        }
        ImGui::EndChild();
    });
    RV_EDITOR_CHECK(!rv_editor_fits_whole(plain, tile).empty());
}

RV_EDITOR_TEST(rv_editor_test_states_with_equal_rectangles_compare_equal_and_a_shifted_one_does_not)
{
    rv_editor_test_imgui gui;
    const auto a = drawn(gui, [] {
        ImGui::Button("One", ImVec2(60, 20));
    });
    const auto b = drawn(gui, [] {
        ImGui::Button("One", ImVec2(60, 20));
    });
    const auto shifted = drawn(gui, [] {
        ImGui::Dummy(ImVec2(1, 5));
        ImGui::Button("One", ImVec2(60, 20));
    });
    RV_EDITOR_CHECK(rv_editor_probe_compare(a, b).empty());
    RV_EDITOR_CHECK(!rv_editor_probe_compare(a, shifted).empty());
}

RV_EDITOR_TEST(rv_editor_test_check_pane_states_fails_one_pixel_under_the_minimum)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui(true);
    const auto kind = rv_editor_pane_kind::controls;
    const stable_tiles::minimum min = stable_tiles::pane_minimum(gui, kind);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    rv_editor_shell shell;
    const std::vector<std::function<void()>> states = { [] {}, [] {} };
    const std::string narrow = rv_editor_check_pane_states(gui, shell, kind, states, { min.w - 1.0f, min.h });
    if (narrow.empty()) {
        std::fprintf(stderr, "no violation at the minimum width minus 1 px\n");
    }
    RV_EDITOR_CHECK(!narrow.empty());
    const std::string low = rv_editor_check_pane_states(gui, shell, kind, states, { min.w, min.h - 1.0f });
    if (low.empty()) {
        std::fprintf(stderr, "no violation at the minimum height minus 1 px\n");
    }
    RV_EDITOR_CHECK(!low.empty());
}
