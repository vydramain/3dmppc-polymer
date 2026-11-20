// S17, R10/R16/R19, AC17: Release Candidate (records with a status, facts whole, the checks table, box, decision shelf),
// Checks (the table in the content area), Observe and Session (facts whole in an area, tables, box).

#include <cmath>
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

constexpr size_t candidate_none = 1;
constexpr size_t candidate_one = 2;
constexpr size_t candidate_several = 3;
constexpr size_t candidate_error = 8;
constexpr size_t checks_none = 1;
constexpr size_t checks_first = 2;
constexpr size_t checks_last = 4;
constexpr size_t observe_pins = 5;
constexpr size_t observe_long = 6;
constexpr size_t session_live = 2;
constexpr size_t session_ended = 3;
constexpr size_t session_crashed = 4;
constexpr size_t session_long = 5;
constexpr size_t check_count = 8;

void report(const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s\n", bad.c_str());
    }
}

// True when every item of `rows` is inside a content area and they all have one width.
bool one_width_in_area(const std::vector<const rv_editor_probe_entry *> &rows)
{
    for (const auto *row : rows) {
        if (!row->in_scroll || std::fabs((row->x1 - row->x0) - (rows[0]->x1 - rows[0]->x0)) > 0.5f) {
            return false;
        }
    }
    return !rows.empty();
}

} // namespace

// R10, S3: the candidates are records of exactly two lines (name, path) with a status, in a content area; with none, the
// area stays and has no record.
RV_EDITOR_TEST(rv_editor_test_s17_candidate_list_is_records_with_a_status_in_a_content_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    float row_h = 0.0f;
    gui.frame([&] {
        row_h = 2.0f * ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
    });
    const auto none = at_roomy(gui, rv_editor_pane_kind::candidate, candidate_none);
    RV_EDITOR_CHECK(!areas(none).empty());
    RV_EDITOR_CHECK(labelled(none, "##row").empty());
    const auto several = at_roomy(gui, rv_editor_pane_kind::candidate, candidate_several);
    const auto rows = labelled(several, "##row");
    RV_EDITOR_CHECK(rows.size() == 3);
    RV_EDITOR_CHECK(one_width_in_area(rows));
    for (const auto *row : rows) {
        if (std::fabs((row->y1 - row->y0) - row_h) > 1.5f) {
            report("a candidate record is not two lines high");
        }
        RV_EDITOR_CHECK(std::fabs((row->y1 - row->y0) - row_h) <= 1.5f);
    }
}

// R16, AC17: the SHA-256 (64 characters) and the image path (100 characters) are shown whole in the facts area; so is the
// report path of the last report.
RV_EDITOR_TEST(rv_editor_test_s17_candidate_facts_show_the_hash_and_the_paths_whole)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto one = at_roomy(gui, rv_editor_pane_kind::candidate, candidate_one);
    RV_EDITOR_CHECK(sha256().size() == sha_chars);
    RV_EDITOR_CHECK(shows_whole(gui, one, sha256()));
    RV_EDITOR_CHECK(shows_whole(gui, one, "/" + solid(path_chars - 1, 'i')));
    const auto error = at_roomy(gui, rv_editor_pane_kind::candidate, candidate_error);
    RV_EDITOR_CHECK(shows_whole(gui, error, "/" + solid(path_chars - 1, 'r')));
}

// R9, R19: the checks are the rows of a table in a content area, one for each check, of one width.
RV_EDITOR_TEST(rv_editor_test_s17_candidate_checks_are_a_table_in_a_content_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto r = at_roomy(gui, rv_editor_pane_kind::candidate, candidate_one);
    const auto rows = labelled(r, "##check");
    RV_EDITOR_CHECK(rows.size() == check_count);
    RV_EDITOR_CHECK(one_width_in_area(rows));
    RV_EDITOR_CHECK(areas(r).size() == 1); // S10: the records, the facts and the checks are sections of one area
}

// R8, R11: the decision buttons are on a shelf (not in an area), in one row, each in one rectangle in every state with a
// candidate; a failure and a refusal are said in the message box.
RV_EDITOR_TEST(rv_editor_test_s17_candidate_has_a_decision_shelf_and_says_a_failure_in_the_box)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::candidate);
    const auto min = tile_of(gui, rv_editor_pane_kind::candidate);
    const char *labels[] = { rv_editor_text("pane_candidate.approve_button"),
        rv_editor_text("pane_candidate.reject_button"),
        rv_editor_text("pane_candidate.undecide_button") };
    rv_editor_probe_record first;
    for (size_t i = candidate_one; i < f.states.size(); ++i) {
        f.states[i].set();
        const auto r = settled(f, min.w, min.h);
        if (first.entries.empty()) {
            first = r;
        }
        float row = -1.0f;
        for (const char *label : labels) {
            const std::string d = item_difference(first, r, label);
            report(std::string(f.states[i].name) + ": " + d);
            RV_EDITOR_CHECK(d.empty());
            const auto *e = find_item(r, label);
            if (e != nullptr) {
                RV_EDITOR_CHECK(!e->in_scroll);
                RV_EDITOR_CHECK(row < 0.0f || std::fabs(e->y0 - row) <= 0.5f);
                row = e->y0;
            }
        }
    }
    RV_EDITOR_CHECK(box_drawn(f, candidate_error) > box_drawn(f, candidate_one));
}

// R9, R19: Checks is its table in a content area in every state with a project: the area stays when there is no candidate
// (with no row), and every check is a row of it.
RV_EDITOR_TEST(rv_editor_test_s17_checks_table_is_in_a_content_area_in_every_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto none = at_roomy(gui, rv_editor_pane_kind::checks, checks_none);
    RV_EDITOR_CHECK(areas(none).size() == 1);
    RV_EDITOR_CHECK(labelled(none, "##check").empty());
    for (size_t i = checks_first; i <= checks_last; ++i) {
        const auto r = at_roomy(gui, rv_editor_pane_kind::checks, i);
        RV_EDITOR_CHECK(areas(r).size() == 1);
        const auto rows = labelled(r, "##check");
        RV_EDITOR_CHECK(rows.size() == check_count);
        RV_EDITOR_CHECK(one_width_in_area(rows));
        RV_EDITOR_CHECK(
            areas(r).size() == 1 && areas(none).size() == 1 && std::fabs(areas(r)[0]->y0 - areas(none)[0]->y0) <= 0.5f);
    }
}

// R16, AC17: Observe's facts are whole in an area (a 100 character disc name is not cut), and its pins and keys are rows
// of tables in areas of their own.
RV_EDITOR_TEST(rv_editor_test_s17_observe_facts_are_whole_and_pins_and_keys_are_tables_in_areas)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto longest = at_roomy(gui, rv_editor_pane_kind::observe, observe_long);
    RV_EDITOR_CHECK(shows_whole(gui, longest, solid(path_chars, 'd')));
    RV_EDITOR_CHECK(shows_whole(gui, longest, solid(path_chars, 'k')));
    const auto r = at_roomy(gui, rv_editor_pane_kind::observe, observe_pins);
    RV_EDITOR_CHECK(areas(r).size() >= 2);
    const auto pins = labelled(r, "##pin");
    const auto keys = labelled(r, "##key");
    RV_EDITOR_CHECK(pins.size() == 2);
    RV_EDITOR_CHECK(keys.size() == 3);
    RV_EDITOR_CHECK(one_width_in_area(pins));
    RV_EDITOR_CHECK(one_width_in_area(keys));
}

// R16, R2: Session's facts and marks are whole, however long (the profile, the disc, a long mark), in the content area, and
// how a session ended is said in the message box.
RV_EDITOR_TEST(rv_editor_test_s17_session_values_are_whole_and_the_end_is_said_in_the_box)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto r = at_roomy(gui, rv_editor_pane_kind::session, session_long);
    RV_EDITOR_CHECK(!areas(r).empty());
    RV_EDITOR_CHECK(shows_whole(gui, r, solid(path_chars, 'f')));
    RV_EDITOR_CHECK(shows_whole(gui, r, solid(path_chars, 'd')));
    RV_EDITOR_CHECK(shows_whole(gui, r, "1. " + s13::words(300)));
    fixture f(gui, rv_editor_pane_kind::session);
    const int live = box_drawn(f, session_live);
    RV_EDITOR_CHECK(box_drawn(f, session_ended) > live);
    RV_EDITOR_CHECK(box_drawn(f, session_crashed) > live);
}
