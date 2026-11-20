// S4 of the panes work, R7, AC8: Search tells a query with no match from a search not run, and Assets tells a filter that
// hid every file (with a reset button) from a project with no resources, in the list and in the icon view. The logs and
// the texts are in rv_editor_test_empty_row.cpp; the empty row helpers are in rv_editor_test_empty_row.hpp.

#include <cstdio>
#include <string>

#include "rv_editor_test.hpp"
#include "rv_editor_test_empty_row.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_s15.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::empty_rows;

// --- Search ----------------------------------------------------------------------------------------------------------

// A query with no match is not a search not yet run: the rows differ, the no-match one names the query, neither has a
// button (the query is the user's, there is nothing to reset to).
RV_EDITOR_TEST(rv_editor_test_empty_row_search_tells_no_match_from_not_asked)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::search);
    f.states[1](); // open, nothing searched
    const record not_asked = s13::settled(f, width, height);
    f.states[2](); // "needle" searched, no hit
    const record no_match = s13::settled(f, width, height);
    const entry *a = empty_row(not_asked);
    const entry *b = empty_row(no_match);
    RV_EDITOR_CHECK(a != nullptr && b != nullptr);
    if (a == nullptr || b == nullptr) {
        return;
    }
    const std::string text_a = row_text(*a);
    const std::string text_b = row_text(*b);
    if (text_a == text_b || !has(text_b, "needle")) {
        report("search", "not asked says '" + text_a + "', no match says '" + text_b + "'");
    }
    RV_EDITOR_CHECK(text_a != text_b);
    RV_EDITOR_CHECK(has(text_b, "needle") && !has(text_a, "needle"));
    RV_EDITOR_CHECK(row_button(not_asked, *a) == nullptr && row_button(no_match, *b) == nullptr);
}

// --- Assets ----------------------------------------------------------------------------------------------------------

// A filter that hid every file: the row says it (names the filter text) with a button that clears it and the files come
// back; a project with no resources says so with no button. In the list and in the icon view.
RV_EDITOR_TEST(rv_editor_test_empty_row_assets_filter_hid_every_file_and_the_reset_button_shows_them)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (const bool details : { true, false }) {
        s15::fixture f(gui, rv_editor_pane_kind::assets);
        f.states[1](); // a project with no resources
        f.app().assets_ui.details = details;
        const record none = f.settled(width, height);
        const entry *none_row = empty_row(none);
        RV_EDITOR_CHECK(none_row != nullptr);
        RV_EDITOR_CHECK(none_row != nullptr && row_button(none, *none_row) == nullptr);

        f.states[2](); // five files, a filter that matches none
        f.app().assets_ui.details = details;
        std::snprintf(f.app().assets_ui.filter, sizeof(f.app().assets_ui.filter), "%s", "zzz");
        const record hidden = f.settled(width, height);
        const entry *row = empty_row(hidden);
        RV_EDITOR_CHECK(row != nullptr);
        if (row == nullptr) {
            continue;
        }
        const entry *button = row_button(hidden, *row);
        const std::string text = row_text(*row);
        if (button == nullptr || !has(text, "zzz") || (none_row != nullptr && text == row_text(*none_row))) {
            report(details ? "assets list" : "assets icons",
                "filter hid every file: row says '" + text + "', button " + (button != nullptr ? "yes" : "no"));
        }
        RV_EDITOR_CHECK(button != nullptr);
        RV_EDITOR_CHECK(has(text, "zzz"));
        RV_EDITOR_CHECK(none_row == nullptr || text != row_text(*none_row));
        if (button == nullptr) {
            continue;
        }
        f.click(*button, width, height);
        const record after = f.settled(width, height);
        RV_EDITOR_CHECK(f.app().assets_ui.filter[0] == '\0');
        RV_EDITOR_CHECK(empty_row(after) == nullptr);
    }
}
