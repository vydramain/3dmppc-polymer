// S10, R12, AC13: Release Candidate is one content area holding the sections Candidates (records), the chosen candidate's
// facts and Checks (the table). Choosing a candidate in Candidates changes the facts and the checks below. In every state
// the pane is that one area and fits whole at its minimum.

#include <cmath>
#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s17_states.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s17;

namespace
{

constexpr size_t state_several = 3; // three candidates, the second chosen
constexpr size_t first_manual = 3;  // Launch: a manual check with a Pass button

// The item a section's title is: its visible label is `title` and it is a band over the area.
const rv_editor_probe_entry *title_of(const rv_editor_probe_record &r, const char *title)
{
    for (const auto &e : r.entries) {
        if (e.in_scroll && e.label.substr(0, e.label.find("##")) == title && e.x1 - e.x0 > 150.0f) {
            return &e;
        }
    }
    return nullptr;
}

void click(fixture &f, const rv_editor_probe_entry &e, float w, float h)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
    f.draw(w, h);
    io.AddMouseButtonEvent(0, true);
    f.draw(w, h);
    io.AddMouseButtonEvent(0, false);
    f.draw(w, h);
    f.draw(w, h);
}

} // namespace

// AC13: one `##area_*` window in every state of the pane that has a project, however much it holds; none without one is
// not asked (the no-project state is a strip alone).
RV_EDITOR_TEST(rv_editor_test_s10_candidate_is_one_area_in_every_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::candidate);
    const auto min = tile_of(gui, rv_editor_pane_kind::candidate);
    for (size_t i = 1; i < f.states.size(); ++i) {
        f.states[i].set();
        const auto r = settled(f, min.w, std::max(min.h, roomy_h));
        if (areas(r).size() != 1) {
            std::fprintf(stderr, "  state %s: %zu areas\n", f.states[i].name, areas(r).size());
        }
        RV_EDITOR_CHECK(areas(r).size() == 1);
    }
}

// The sections, top to bottom: the Candidates title, the records, the chosen candidate's facts, the Checks title, the
// checks table; all inside the area, the titles as bands over it.
RV_EDITOR_TEST(rv_editor_test_s10_candidate_area_holds_the_three_sections_in_order)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto r = at_roomy(gui, rv_editor_pane_kind::candidate, state_several);
    const auto *candidates = title_of(r, "Candidates");
    const auto *checks = title_of(r, "Checks");
    RV_EDITOR_CHECK(candidates != nullptr);
    RV_EDITOR_CHECK(checks != nullptr);
    const auto records = labelled(r, "##row");
    const auto rows = labelled(r, "##check");
    RV_EDITOR_CHECK(records.size() == 3);
    RV_EDITOR_CHECK(rows.size() == 8);
    if (candidates == nullptr || checks == nullptr || records.size() != 3 || rows.empty()) {
        return;
    }
    RV_EDITOR_CHECK(candidates->y1 <= records[0]->y0 + 0.5f);
    const float records_end = records[2]->y1;
    RV_EDITOR_CHECK(records_end <= checks->y0 + 0.5f);
    RV_EDITOR_CHECK(checks->y1 <= rows[0]->y0 + 0.5f);
    // The facts are between the records and the Checks title: the hash (64 characters) is there, whole.
    const float need = width_of(gui, sha256()) - 0.5f;
    bool facts_between = false;
    for (const auto &e : r.entries) {
        if (e.in_scroll && e.label.empty() && !e.scroll_area && e.x1 - e.x0 >= need && e.y0 >= records_end - 0.5f &&
            e.y1 <= checks->y0 + 0.5f) {
            facts_between = true;
        }
    }
    RV_EDITOR_CHECK(facts_between);
}

// AC13: choosing a candidate in Candidates changes the facts and the checks. Candidate 1 has a 64 character hash and
// candidate 3 a short one; each has its own checks, so a double click on the first manual row passes the chosen one's check
// only.
RV_EDITOR_TEST(rv_editor_test_s10_choosing_a_candidate_changes_its_facts_and_checks)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::candidate);
    const auto min = tile_of(gui, rv_editor_pane_kind::candidate);
    const float h = std::max(min.h, roomy_h);
    f.states[state_several].set();
    auto &list = f.app().release.candidates;
    list[0].image = "/i";
    list[1].image = "/i";
    list[2].image = "/i";
    list[2].sha256 = "abc";
    f.app().release.report.clear();
    f.app().release.selected = 1; // candidate 2, as the hash is the same long one
    const float need = width_of(gui, sha256()) - 0.5f;
    const auto long_hash_shown = [&](const rv_editor_probe_record &r) {
        for (const auto &e : r.entries) {
            if (e.in_scroll && e.label.empty() && !e.scroll_area && e.x1 - e.x0 >= need) {
                return true;
            }
        }
        return false;
    };
    auto r = settled(f, min.w, h);
    RV_EDITOR_CHECK(areas(r).size() == 1); // all of it in the one area
    RV_EDITOR_CHECK(long_hash_shown(r));
    // Choose candidate 3 (the third record): its short hash is shown, the long one is not.
    auto records = labelled(r, "##row");
    RV_EDITOR_CHECK(records.size() == 3);
    if (records.size() != 3) {
        return;
    }
    click(f, *records[2], min.w, h);
    RV_EDITOR_CHECK(f.app().release.selected == 2);
    r = settled(f, min.w, h);
    RV_EDITOR_CHECK(!long_hash_shown(r));
    // Its checks are the table's: a double click on the first manual row passes candidate 3's check and no other.
    list[2].sha256 = sha256(); // a hash is needed to pass; the checks stay candidate 3's
    r = settled(f, min.w, h);
    const auto rows = labelled(r, "##check");
    RV_EDITOR_CHECK(rows.size() == 8);
    if (rows.size() == 8) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(rows[first_manual]->x0 + 12.0f, (rows[first_manual]->y0 + rows[first_manual]->y1) / 2.0f);
        f.draw(min.w, h);
        for (int i = 0; i < 2; ++i) { // a double click on the row passes it, as the Pass button does
            io.AddMouseButtonEvent(0, true);
            f.draw(min.w, h);
            io.AddMouseButtonEvent(0, false);
            f.draw(min.w, h);
        }
        f.draw(min.w, h);
        RV_EDITOR_CHECK(list[2].checks[first_manual].state == rv_editor_check_state::passed);
        RV_EDITOR_CHECK(list[1].checks[first_manual].state == rv_editor_check_state::not_run);
        RV_EDITOR_CHECK(list[0].checks[first_manual].state == rv_editor_check_state::not_run);
    }
    // Back to candidate 1: its checks are unset and its facts are the long hash again.
    r = settled(f, min.w, h);
    records = labelled(r, "##row");
    if (records.size() == 3) {
        click(f, *records[0], min.w, h);
        RV_EDITOR_CHECK(f.app().release.selected == 0);
        r = settled(f, min.w, h);
        RV_EDITOR_CHECK(long_hash_shown(r));
    }
}

// R12, AC13: at the pane's minimum every state fits whole (nothing outside the tile, no cut text, no ellipsis, no scroll
// bar outside the one area) and is the one area; Checks too.
RV_EDITOR_TEST(rv_editor_test_s10_candidate_and_checks_fit_whole_at_the_minimum_as_one_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : { rv_editor_pane_kind::candidate, rv_editor_pane_kind::checks }) {
        fixture f(gui, kind);
        const auto min = tile_of(gui, kind);
        for (size_t i = 0; i < f.states.size(); ++i) {
            f.states[i].set();
            const auto r = settled(f, min.w, min.h);
            const std::string bad = rv_editor_fits_whole(r, ImRect(0, 0, min.w, min.h));
            if (!bad.empty()) {
                std::fprintf(stderr, "  %s, state %s: %s\n", name_of(kind), f.states[i].name, bad.c_str());
            }
            RV_EDITOR_CHECK(bad.empty());
            if (f.states[i].project) {
                if (areas(r).size() != 1) {
                    std::fprintf(stderr, "  %s, state %s: %zu areas\n", name_of(kind), f.states[i].name, areas(r).size());
                }
                RV_EDITOR_CHECK(areas(r).size() == 1);
            }
        }
    }
}
