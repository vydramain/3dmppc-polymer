// S15, R10/R11/R13/R16/R19/R20: what Project, Assets, Findings and Test Case put where. Project: a message box for what
// is wrong, the facts list (S9) and the tools as records with a status (S3) in content areas; Assets: a shelf, a message
// box and the grid or the table in a content area; Findings and Test Case: the form (S6), a message box and the lists in
// content areas. Every state keeps these where they are (the fits test compares the windows; here the order and the
// content).

#include <cmath>
#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s15;

namespace
{

constexpr float roomy_w = 760.0f;
constexpr float roomy_h = 560.0f;
constexpr int tool_records = 7; // runtime, burner, firmware, three bakers, inspector

void report(const char *what, const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s: %s\n", what, bad.c_str());
    }
}

// "" when the record has a message box over a content area, no ask place and nothing older; else what is wrong.
std::string box_over_area(const rv_editor_probe_record &r)
{
    const auto *box = message_box(r);
    const auto all = areas(r);
    if (box == nullptr) {
        return "no message box";
    }
    if (all.empty()) {
        return "no content area";
    }
    if (has_ask_place(r)) {
        return "an ask place is still drawn";
    }
    for (const auto *a : all) {
        if (a->y0 < box->y1) {
            return "a content area starts above the message box ends";
        }
    }
    return "";
}

} // namespace

// R16, R10, R11: Project in every state has the box above its areas; the tools are seven records in the areas and the
// facts list is the first area, whose row menu copies a fact.
RV_EDITOR_TEST(rv_editor_test_s15_project_has_a_box_facts_and_tool_records)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::project);
    for (const ImVec2 size : tile_sizes(gui, f.kind, ImVec2(roomy_w, roomy_h))) {
        for (size_t i = 0; i < f.states.size(); ++i) {
            const auto r = snap(f, i, size.x, size.y);
            const std::string bad = box_over_area(r);
            report("project", bad);
            RV_EDITOR_CHECK(bad.empty());
            int records = 0;
            for (const auto *a : areas(r)) {
                records += labelled_in(r, *a, "##row");
            }
            if (size.x == roomy_w && records != tool_records) {
                std::fprintf(stderr, "  project state %zu: %d tool records, %d expected\n", i, records, tool_records);
            }
            // At the minimum the areas show what fits; at a roomy size all seven are there.
            RV_EDITOR_CHECK(size.x != roomy_w || records == tool_records);
            RV_EDITOR_CHECK(find_text(r, rv_editor_text("panes.look_for_tools_button")) != nullptr);
        }
    }
}

// R11: a manifest error, a tool that cannot run and a settings error are the box's text, each drawn where the idle
// project has an empty box.
RV_EDITOR_TEST(rv_editor_test_s15_project_says_what_is_wrong_in_the_box)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::project);
    const auto idle = snap(f, 1, roomy_w, roomy_h);
    for (const size_t i : { size_t(2), size_t(3), size_t(4) }) {
        const auto r = snap(f, i, roomy_w, roomy_h);
        RV_EDITOR_CHECK(message_box(r) != nullptr && message_box(idle) != nullptr);
        if (r.drawn <= idle.drawn) {
            std::fprintf(stderr, "  project state %zu: the box draws %d, the idle box %d\n", i, r.drawn, idle.drawn);
        }
        RV_EDITOR_CHECK(r.drawn > idle.drawn);
    }
}

// R16: a right click on the first row of the facts list opens its menu, and Copy puts one of the project's facts on the
// clipboard.
RV_EDITOR_TEST(rv_editor_test_s15_project_facts_row_menu_copies_a_fact)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::project);
    const std::string copy = rv_editor_text("widgets_fields.button_copy");
    f.states[1]();
    auto r = f.settled(roomy_w, roomy_h);
    const auto all = areas(r);
    RV_EDITOR_CHECK(!all.empty());
    if (all.empty()) {
        return;
    }
    float line = 0.0f;
    gui.frame([&] {
        line = ImGui::GetTextLineHeight();
    });
    rv_editor_probe_entry row = *all[0];
    row.x1 = row.x0 + 2.0f * line; // the key column's left end
    row.y1 = row.y0 + 2.0f * line; // the first row, under the area's frame and gap
    ImGui::SetClipboardText("");
    f.click(row, roomy_w, roomy_h, 1);
    r = f.settled(roomy_w, roomy_h);
    const auto *item = find_text(r, copy);
    RV_EDITOR_CHECK(item != nullptr);
    if (item == nullptr) {
        return;
    }
    f.click(*item, roomy_w, roomy_h);
    const char *clip = ImGui::GetClipboardText();
    const std::string got = clip != nullptr ? clip : "";
    const auto &p = f.app().project;
    const std::string facts[] = { "disc",
        p.root.string(),
        (p.cache_dir / "builds").string(),
        (p.state_dir / "memcard.mppccard").string(),
        f.app().tools.settings_path.string() };
    bool known = false;
    for (const auto &fact : facts) {
        known = known || got == fact;
    }
    if (!known) {
        std::fprintf(stderr, "  project: Copy put '%s' on the clipboard\n", got.c_str());
    }
    RV_EDITOR_CHECK(known);
}

// R8, R11, R9: Assets is a shelf (folder, filter, Details, Refresh in one row), a message box and the resources in a
// content area as a grid of cells or as a table of rows, in every state.
RV_EDITOR_TEST(rv_editor_test_s15_assets_have_a_shelf_a_box_and_the_resources_in_an_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::assets);
    // 0 no project, 1 empty, 2 no matches, 3 grid, 4 table, 5-8 previews.
    for (const ImVec2 size : tile_sizes(gui, f.kind, ImVec2(roomy_w, roomy_h))) {
        for (size_t i = 0; i < f.states.size(); ++i) {
            const auto r = snap(f, i, size.x, size.y);
            const std::string bad = box_over_area(r);
            report("assets", bad);
            RV_EDITOR_CHECK(bad.empty());
            const auto *box = message_box(r);
            const auto *folder = find_id(r, "##folder");
            const auto *filter = find_id(r, "##filter");
            const auto *details = find_text(r, rv_editor_text("pane_assets.details"));
            const auto *refresh = find_text(r, rv_editor_text("pane_assets.refresh"));
            RV_EDITOR_CHECK(folder != nullptr && filter != nullptr && details != nullptr && refresh != nullptr);
            if (box != nullptr && folder != nullptr && filter != nullptr && details != nullptr && refresh != nullptr) {
                RV_EDITOR_CHECK(folder->y1 <= box->y0 && filter->y1 <= box->y0 && refresh->y1 <= box->y0);
                RV_EDITOR_CHECK(std::abs(folder->y0 - filter->y0) <= 1.0f);
                RV_EDITOR_CHECK(details->y0 < box->y0 && refresh->x0 > filter->x1); // one row, in this order
            }
        }
    }
    const auto grid = snap(f, 3, roomy_w, roomy_h);
    const auto table = snap(f, 4, roomy_w, roomy_h);
    const auto *area = content_area(grid);
    const auto *table_area = content_area(table);
    RV_EDITOR_CHECK(area != nullptr && table_area != nullptr);
    if (area != nullptr && table_area != nullptr) {
        int cells = 0;
        for (const auto &e : grid.entries) {
            cells += e.label == "##cell" && inside(e, *area) ? 1 : 0;
        }
        RV_EDITOR_CHECK(cells == 5);
        RV_EDITOR_CHECK(labelled_in(table, *table_area, "###row") >= 5);
    }
}

// R4: no resources and a filter without matches say so in the area's first row, which a grid with resources does not
// have.
RV_EDITOR_TEST(rv_editor_test_s15_assets_empty_states_say_so_in_the_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::assets);
    for (const size_t i : { size_t(1), size_t(2), size_t(3) }) {
        const auto r = snap(f, i, roomy_w, roomy_h);
        const auto *area = content_area(r);
        RV_EDITOR_CHECK(area != nullptr);
        if (area != nullptr) {
            const int empty = labelled_in(r, *area, "###empty");
            RV_EDITOR_CHECK(i == 3 ? empty == 0 : empty == 1);
        }
    }
}

// R13, R11, R3: Findings is a shelf with Capture Frame and Record Finding, the message box, the form whose fields share
// one column right of the longest label, and the saved findings in a content area.
RV_EDITOR_TEST(rv_editor_test_s15_findings_are_a_shelf_a_box_a_form_and_a_list)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::findings);
    float pad = 0.0f;
    float label_w = 0.0f;
    gui.frame([&] {
        pad = ImGui::GetStyle().WindowPadding.x;
        label_w = ImGui::CalcTextSize(rv_editor_text("findings.expected")).x;
    });
    for (const ImVec2 size : tile_sizes(gui, f.kind, ImVec2(roomy_w, roomy_h))) {
        for (size_t i = 0; i < f.states.size(); ++i) {
            const auto r = snap(f, i, size.x, size.y);
            const std::string bad = box_over_area(r);
            report("findings", bad);
            RV_EDITOR_CHECK(bad.empty());
            const auto *capture = find_text(r, rv_editor_text("findings.capture_frame"));
            const auto *record = find_text(r, rv_editor_text("findings.record_finding"));
            const auto *box = message_box(r);
            RV_EDITOR_CHECK(capture != nullptr && record != nullptr && box != nullptr);
            if (capture != nullptr && record != nullptr && box != nullptr) {
                RV_EDITOR_CHECK(capture->y0 == record->y0 && capture->x1 <= record->x0 && record->y1 <= box->y0);
            }
        }
    }
    const auto r = snap(f, 1, roomy_w, roomy_h);
    const auto *title = find_id(r, "##Title");
    RV_EDITOR_CHECK(title != nullptr);
    if (title != nullptr) {
        RV_EDITOR_CHECK(title->x0 >= pad + label_w); // right of the longest label of the form
        RV_EDITOR_CHECK(message_box(r) != nullptr && title->y0 >= message_box(r)->y1);
    }
}

// R11: a finding's error is the box's text with Dismiss in the box's button row; the saved findings are rows of the
// list, one each.
RV_EDITOR_TEST(rv_editor_test_s15_findings_error_is_in_the_box_and_saved_ones_are_rows)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::findings);
    const auto idle = snap(f, 1, roomy_w, roomy_h);
    const auto error = snap(f, 2, roomy_w, roomy_h);
    RV_EDITOR_CHECK(error.drawn > idle.drawn);
    const auto *dismiss = find_text(error, rv_editor_text("findings.dismiss"));
    RV_EDITOR_CHECK(dismiss != nullptr && message_box(error) != nullptr && dismiss->y0 >= message_box(error)->y1);
    const auto saved = snap(f, 4, roomy_w, roomy_h);
    const auto *area = content_area(saved);
    RV_EDITOR_CHECK(area != nullptr);
    if (area != nullptr) {
        RV_EDITOR_CHECK(items_in(saved, *area) >= 6);
        const auto *empty_area = content_area(idle);
        RV_EDITOR_CHECK(empty_area != nullptr && labelled_in(idle, *empty_area, "###empty") == 1); // R4
    }
}

// R13, R11: Test Case keeps Passed, Failed and Blocked in one row at one place in every state, the box above them says
// the result line, the error and the missing session, and the results are rows of a list.
RV_EDITOR_TEST(rv_editor_test_s15_test_case_has_a_form_a_box_and_lists)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::test_case);
    const std::string passed = rv_editor_text("findings.passed");
    const std::string failed = rv_editor_text("findings.failed");
    const std::string blocked = rv_editor_text("findings.blocked");
    for (const ImVec2 size : tile_sizes(gui, f.kind, ImVec2(roomy_w, roomy_h))) {
        std::vector<shot> r;
        for (size_t i = 0; i < f.states.size(); ++i) {
            r.push_back(snap(f, i, size.x, size.y));
            const std::string bad = box_over_area(r.back());
            report("test_case", bad);
            RV_EDITOR_CHECK(bad.empty());
            const auto *a = find_text(r.back(), passed);
            const auto *b = find_text(r.back(), failed);
            const auto *c = find_text(r.back(), blocked);
            RV_EDITOR_CHECK(a != nullptr && b != nullptr && c != nullptr);
            if (a != nullptr && b != nullptr && c != nullptr) {
                RV_EDITOR_CHECK(a->y0 == b->y0 && b->y0 == c->y0 && a->x1 <= b->x0 && b->x1 <= c->x0);
            }
            const std::string moved = same_items(r.front(), r.back(), { passed, failed, blocked });
            report("test_case", moved);
            RV_EDITOR_CHECK(moved.empty());
        }
        if (r.size() == 6) {
            RV_EDITOR_CHECK(r[5].drawn > r[2].drawn); // the error is the box's text
        }
    }
    // 4: results are rows of a list.
    const auto none = snap(f, 2, roomy_w, roomy_h);
    const auto results = snap(f, 4, roomy_w, roomy_h);
    int rows_none = 0;
    int rows_some = 0;
    for (const auto *a : areas(none)) {
        rows_none += items_in(none, *a);
    }
    for (const auto *a : areas(results)) {
        rows_some += items_in(results, *a);
    }
    RV_EDITOR_CHECK(rows_some > rows_none);
}
