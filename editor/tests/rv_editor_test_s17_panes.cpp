// S17, R10/R19/R24: Export (form with the kind dropdown and its two-line description, steps in the content area, message
// box, result shelf), Build Result (shelf, box, log in the area) and the empty pane (text and kind buttons in the area).

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s17_states.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s17;
using namespace rv_editor_test::pane_state;

namespace
{

constexpr size_t export_disc = 1;
constexpr size_t export_running = 5;
constexpr size_t export_failed = 7;
constexpr size_t export_refused = 9;
constexpr size_t build_idle = 1;
constexpr size_t build_failed = 4;

void report(const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s\n", bad.c_str());
    }
}

// "" when each button of `labels` is in `a` and `b` at one rectangle, outside every content area, and all sit in one row.
std::string
shelf_difference(const rv_editor_probe_record &a, const rv_editor_probe_record &b, const std::vector<const char *> &labels)
{
    float row = -1.0f;
    for (const char *label : labels) {
        if (const std::string d = item_difference(a, b, label); !d.empty()) {
            return d;
        }
        const auto *e = find_item(b, label);
        if (e->in_scroll) {
            return std::string("'") + label + "' is inside a content area, not on a shelf";
        }
        if (row >= 0.0f && (e->y0 < row - 0.5f || e->y0 > row + 0.5f)) {
            return std::string("'") + label + "' is not in the row of the others";
        }
        row = e->y0;
    }
    return "";
}

} // namespace

// R24, R13: the kind is a dropdown over a two-line description on Catppuccin, which is under the dropdown and over the
// steps; the dropdown is not a card per kind.
RV_EDITOR_TEST(rv_editor_test_s17_export_form_has_the_kind_dropdown_over_a_two_line_description)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    float want = 0.0f;
    gui.frame([&] {
        want = 2.0f * ImGui::GetTextLineHeight() + 2.0f * ImGui::GetStyle().WindowPadding.y;
    });
    for (const size_t state : { export_disc, export_disc + 1, export_disc + 2 }) { // disc, player, web
        const auto r = at_minimum(gui, rv_editor_pane_kind::export_, state);
        const auto *combo = find_item(r, "##export_kind_combo");
        RV_EDITOR_CHECK(combo != nullptr);
        RV_EDITOR_CHECK(find_item(r, "##export_card_0") == nullptr);
        const auto t = texts(r);
        const auto a = areas(r);
        RV_EDITOR_CHECK(t.size() >= 2);
        RV_EDITOR_CHECK(!a.empty());
        if (combo == nullptr || t.size() < 2 || a.empty()) {
            continue;
        }
        // The description is the first text window: under the dropdown, over the steps, two lines.
        RV_EDITOR_CHECK(t[0]->y0 >= combo->y1 - 0.5f);
        RV_EDITOR_CHECK(t[0]->y1 <= a[0]->y0 + 0.5f);
        report(t[0]->y1 - t[0]->y0 > want + 0.6f || t[0]->y1 - t[0]->y0 < want - 0.6f ?
                "the description is not two lines high" :
                "");
        RV_EDITOR_CHECK(t[0]->y1 - t[0]->y0 >= want - 0.6f && t[0]->y1 - t[0]->y0 <= want + 0.6f);
    }
}

// R3, AC1: the steps of a running export are in the content area, and a short run (three steps) needs no bar inside it.
RV_EDITOR_TEST(rv_editor_test_s17_export_steps_are_in_the_content_area_without_bars_when_short)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto r = at_roomy(gui, rv_editor_pane_kind::export_, export_running);
    const auto a = areas(r);
    RV_EDITOR_CHECK(!a.empty());
    int in_area = 0;
    for (const auto &e : r.entries) {
        in_area += e.in_scroll && e.label.empty() ? 1 : 0;
        if (s13::is_area_scroll(e)) {
            RV_EDITOR_CHECK(!e.scrollbar);
            if (e.scrollbar) {
                report("a short run shows a bar inside the steps area");
            }
        }
    }
    RV_EDITOR_CHECK(in_area >= 3);
}

// R8, R24: Export, Cancel and the three result buttons are on shelves in every state with a project, each in its own
// rectangle whatever the state.
RV_EDITOR_TEST(rv_editor_test_s17_export_buttons_keep_their_rectangles_in_every_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::export_);
    const auto min = tile_of(gui, rv_editor_pane_kind::export_);
    const std::vector<const char *> labels = { rv_editor_text("pane_export.export_button"),
        rv_editor_text("pane_export.cancel_button"),
        rv_editor_text("pane_export.open_folder_button"),
        rv_editor_text("pane_export.open_browser_button"),
        rv_editor_text("pane_export.show_problems_button") };
    rv_editor_probe_record first;
    for (size_t i = 0; i < f.states.size(); ++i) {
        if (!f.states[i].project) {
            continue;
        }
        f.states[i].set();
        const auto r = settled(f, min.w, min.h);
        if (first.entries.empty()) {
            first = r;
        }
        for (const char *label : labels) {
            RV_EDITOR_CHECK(find_item(r, label) != nullptr);
        }
        const std::string d = shelf_difference(first, r, { labels[2], labels[3], labels[4] });
        report(std::string(f.states[i].name) + ": " + d);
        RV_EDITOR_CHECK(d.empty());
        for (const char *label : { labels[0], labels[1] }) {
            const std::string moved = item_difference(first, r, label);
            report(std::string(f.states[i].name) + ": " + moved);
            RV_EDITOR_CHECK(moved.empty());
        }
    }
}

// R11, R20: a refusal and a failed export are said in the message box: its text windows draw more than they do in a
// state with nothing to say.
RV_EDITOR_TEST(rv_editor_test_s17_export_says_a_refusal_and_a_failure_in_the_message_box)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::export_);
    const int quiet = box_drawn(f, export_disc);
    const int refused = box_drawn(f, export_refused);
    const int failed = box_drawn(f, export_failed);
    RV_EDITOR_CHECK(quiet > 0);
    RV_EDITOR_CHECK(refused > quiet);
    RV_EDITOR_CHECK(failed > quiet);
}

// R10, R19: Build Result keeps its three buttons in one row of one shelf in every state, the message box under it and the
// log area under the box; Cancel Build is always there.
RV_EDITOR_TEST(rv_editor_test_s17_build_result_has_a_shelf_over_the_box_over_the_log_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::build_result);
    const auto min = tile_of(gui, rv_editor_pane_kind::build_result);
    const std::vector<const char *> labels = { rv_editor_text("pane_build_result.build_button"),
        rv_editor_text("pane_build_result.build_candidate_button"),
        rv_editor_text("pane_build_result.cancel_button") };
    rv_editor_probe_record first;
    for (size_t i = 0; i < f.states.size(); ++i) {
        if (!f.states[i].project) {
            continue;
        }
        f.states[i].set();
        const auto r = settled(f, min.w, min.h);
        if (first.entries.empty()) {
            first = r;
        }
        const std::string d = shelf_difference(first, r, labels);
        report(std::string(f.states[i].name) + ": " + d);
        RV_EDITOR_CHECK(d.empty());
        const auto t = texts(r);
        const auto a = areas(r);
        RV_EDITOR_CHECK(t.size() == 1 && a.size() == 1);
        const auto *cancel = find_item(r, labels[2]);
        if (t.size() == 1 && a.size() == 1 && cancel != nullptr) {
            RV_EDITOR_CHECK(cancel->y1 <= t[0]->y0 + 0.5f);
            RV_EDITOR_CHECK(t[0]->y1 <= a[0]->y0 + 0.5f);
        }
    }
}

// R11, R19: the job's own lines are rows of the log area, and what the job did is said in the message box.
RV_EDITOR_TEST(rv_editor_test_s17_build_result_log_rows_are_in_the_area_and_the_box_says_the_outcome)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::build_result);
    const auto r = at_roomy(gui, rv_editor_pane_kind::build_result, build_failed);
    int rows = 0;
    for (const auto &e : r.entries) {
        rows += e.in_scroll ? 1 : 0;
    }
    RV_EDITOR_CHECK(!areas(r).empty());
    RV_EDITOR_CHECK(rows >= 3);
    // A fixture of its own per state: build.state() of the first one stays failed whatever is set after it.
    fixture idle(gui, rv_editor_pane_kind::build_result);
    RV_EDITOR_CHECK(box_drawn(f, build_failed) > box_drawn(idle, build_idle));
}

// R19: the empty pane's text and one button for every other kind are items of the content area (they scroll, the tile does
// not), and the pane shows no message box.
RV_EDITOR_TEST(rv_editor_test_s17_empty_pane_text_and_kind_buttons_are_in_the_content_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::empty);
    const auto min = tile_of(gui, rv_editor_pane_kind::empty);
    const auto r = settled(f, min.w, std::max(min.h, roomy_h));
    RV_EDITOR_CHECK(areas(r).size() == 1);
    int text = 0;
    for (const auto &e : r.entries) {
        text += e.in_scroll && e.label.empty() ? 1 : 0;
    }
    RV_EDITOR_CHECK(text >= 1);
    for (uint32_t k = 0; k <= static_cast<uint32_t>(rv_editor_pane_kind_last); ++k) {
        const auto kind = static_cast<rv_editor_pane_kind>(k);
        if (kind == rv_editor_pane_kind::empty) {
            continue;
        }
        const auto *button = find_item(r, rv_editor_pane_title(kind));
        RV_EDITOR_CHECK(button != nullptr && button->in_scroll);
        if (button == nullptr || !button->in_scroll) {
            std::fprintf(stderr, "  no button '%s' in the content area\n", rv_editor_pane_title(kind));
        }
    }
}
