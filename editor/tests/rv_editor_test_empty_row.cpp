// S4 of the panes work, R7, AC8: the empty row of a table says whether nothing came or a filter hid it. In Console Output,
// Runtime Log and Build Log a log with N lines that a filter hid names N and the filter (the first of Source, Level, Find,
// Clear View that hid them) and has the filter's reset button in the same row; pressing it shows the N lines. A log with no
// lines says so and has no button. A strip about hidden lines is gone: the row says it, and with a line in sight nothing
// does. Search tells a query with no match from a search not run; Assets tells a filter that hid every file (with a reset
// button) from a project with no resources. Findings has no filter: its empty row is the list of saved ones, no button.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_s15.hpp"
#include "rv_editor_test_strip.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_content.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr float width = 900.0f;
constexpr float height = 640.0f;
constexpr int hidden_count = 7;
constexpr rv_editor_theme theme = rv_editor_theme_olive;

using entry = rv_editor_probe_entry;
using record = rv_editor_probe_record;

// The empty row is the item whose id is "empty" (rv_editor_content_empty_row), its text is the label before the marker.
const entry *empty_row(const record &r)
{
    for (const auto &e : r.entries) {
        if (e.label.find("###empty") != std::string::npos) {
            return &e;
        }
    }
    return nullptr;
}

std::string row_text(const entry &row)
{
    return row.label.substr(0, row.label.find("###empty"));
}

bool has(const std::string &text, const char *word)
{
    return text.find(word) != std::string::npos;
}

// A named item inside a content area, other than the empty row, whose middle lies within the row's height: the button of
// the row.
const entry *row_button(const record &r, const entry &row)
{
    for (const auto &e : r.entries) {
        const float middle = (e.y0 + e.y1) / 2.0f;
        if (&e != &row && e.in_scroll && !e.scroll_area && e.id != 0 && !e.label.empty() && e.x1 - e.x0 > 1.0f &&
            middle >= row.y0 - 1.0f && middle <= row.y1 + 1.0f) {
            return &e;
        }
    }
    return nullptr;
}

int log_rows(const record &r)
{
    int n = 0;
    for (const auto &e : r.entries) {
        n += e.in_scroll && e.label == "##row" ? 1 : 0;
    }
    return n;
}

std::string source_of_toml()
{
    const std::filesystem::path file =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "texts" / "rv_editor_texts.toml";
    std::ifstream in(file);
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

void report(const char *what, const std::string &detail)
{
    std::fprintf(stderr, "  %s: %s\n", what, detail.c_str());
}

// --- logs ------------------------------------------------------------------------------------------------------------

enum class by {
    source,
    level,
    find,
    clear_view
};

const char *filter_name(by w)
{
    return w == by::source ? "Source" : w == by::level ? "Level" : w == by::find ? "Find" : "Clear View";
}

const std::vector<by> all_filters = { by::source, by::level, by::find, by::clear_view };
const std::vector<rv_editor_pane_kind> log_kinds = { rv_editor_pane_kind::output,
    rv_editor_pane_kind::runtime_log,
    rv_editor_pane_kind::build_log };

// The lines of the log's own source (Runtime Log shows runtime, Build Log candidate by default), else the editor's.
rv_editor_log_source own_source(rv_editor_pane_kind kind)
{
    return kind == rv_editor_pane_kind::runtime_log ? rv_editor_log_source::runtime :
        kind == rv_editor_pane_kind::build_log      ? rv_editor_log_source::candidate :
                                                      rv_editor_log_source::editor;
}

void add_lines(s13::fixture &f, int count, rv_editor_log_level level, const char *text, rv_editor_log_source source)
{
    for (int i = 0; i < count; ++i) {
        f.app().log.add(source, level, std::string(text) + " " + std::to_string(i));
    }
}

// The pane drawn once (the runtime and build logs make their own view then), then `hidden_count` lines that filter `w` hides.
void hide_all(s13::fixture &f, rv_editor_pane_kind kind, by w, int count = hidden_count)
{
    const rv_editor_log_source own = own_source(kind);
    f.draw(width, height);
    rv_editor_output_view &v = f.app().outputs[s13::pane];
    switch (w) {
    case by::source:
        add_lines(f, count, rv_editor_log_level::warning, "kept out by source", rv_editor_log_source::editor);
        v.show.fill(false);
        break;
    case by::level:
        add_lines(f, count, rv_editor_log_level::warning, "kept out by level", own);
        v.level = rv_editor_log_level::error;
        break;
    case by::find:
        add_lines(f, count, rv_editor_log_level::warning, "kept out by find", own);
        std::snprintf(v.search, sizeof(v.search), "%s", "zzz");
        break;
    case by::clear_view:
        add_lines(f, count, rv_editor_log_level::warning, "kept out by clear view", own);
        v.hide_before = f.app().log.revision() + 1;
        break;
    }
}

void press(s13::fixture &f, const entry &e)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
    f.draw(width, height);
    io.AddMouseButtonEvent(0, true);
    f.draw(width, height);
    io.AddMouseButtonEvent(0, false);
    f.draw(width, height);
    io.AddMousePosEvent(-1000.0f, -1000.0f);
    f.draw(width, height);
}

// Presses the button of the row; false when there is no row or no button.
bool press_reset(s13::fixture &f)
{
    const record r = s13::settled(f, width, height);
    const entry *row = empty_row(r);
    const entry *button = row != nullptr ? row_button(r, *row) : nullptr;
    if (button == nullptr) {
        return false;
    }
    press(f, *button);
    return true;
}

// The bottom of the last content area of a log whose view is set by `set` after the first draw.
template <class F>
float area_bottom(rv_editor_test_imgui &gui, rv_editor_pane_kind kind, F set)
{
    s13::fixture f(gui, kind);
    f.draw(width, height);
    set(f);
    const record r = s13::settled(f, width, height);
    const entry *area = strip::last_area(r);
    return area != nullptr ? area->y1 : -1.0f;
}

} // namespace

// The widget: a button in the row is drawn in the row's height and its press is the result; without one the row is one item.
RV_EDITOR_TEST(rv_editor_test_empty_row_button_stands_in_the_row_and_reports_its_press)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (const bool with_button : { true, false }) {
        bool pressed = false;
        const auto draw = [&] {
            return rv_editor_probe_draw(
                gui,
                400.0f,
                [&](record &) {
                    (void)rv_editor_content_begin("c", ImVec2(360.0f, 120.0f), theme);
                    pressed =
                        rv_editor_content_empty_row("7 lines hidden by Source", theme, with_button ? "Show Source" : nullptr) ||
                        pressed;
                    rv_editor_content_end(theme);
                },
                300.0f);
        };
        record r = draw();
        r = draw();
        const entry *row = empty_row(r);
        RV_EDITOR_CHECK(row != nullptr);
        if (row == nullptr) {
            continue;
        }
        RV_EDITOR_CHECK(has(row_text(*row), "7 lines hidden by Source"));
        const entry *button = row_button(r, *row);
        RV_EDITOR_CHECK((button != nullptr) == with_button);
        const entry &target = button != nullptr ? *button : *row;
        pressed = false;
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent((target.x0 + target.x1) / 2.0f, (target.y0 + target.y1) / 2.0f);
        draw();
        io.AddMouseButtonEvent(0, true);
        draw();
        io.AddMouseButtonEvent(0, false);
        draw();
        io.AddMousePosEvent(-1000.0f, -1000.0f);
        draw();
        RV_EDITOR_CHECK(pressed == with_button);
    }
}

// AC8: N lines hidden by one filter: the row names N and the filter, and carries the reset button; no "No lines yet".
RV_EDITOR_TEST(rv_editor_test_empty_row_log_says_how_many_lines_which_filter_hid)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (const auto kind : log_kinds) {
        for (const by w : all_filters) {
            s13::fixture f(gui, kind);
            hide_all(f, kind, w);
            const record r = s13::settled(f, width, height);
            const entry *row = empty_row(r);
            const std::string what = std::string(s13::name_of(kind)) + " hidden by " + filter_name(w);
            RV_EDITOR_CHECK(row != nullptr);
            if (row == nullptr) {
                report(what.c_str(), "no empty row");
                continue;
            }
            const std::string text = row_text(*row);
            const bool ok = has(text, std::to_string(hidden_count).c_str()) && has(text, filter_name(w)) &&
                !has(text, "No lines yet") && row_button(r, *row) != nullptr;
            if (!ok) {
                report(what.c_str(), "row says '" + text + "', button " + (row_button(r, *row) != nullptr ? "yes" : "no"));
            }
            RV_EDITOR_CHECK(ok);
        }
    }
}

// One hidden line is "1 line", not "1 lines".
RV_EDITOR_TEST(rv_editor_test_empty_row_log_one_hidden_line_is_singular)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (const auto kind : log_kinds) {
        for (const by w : all_filters) {
            s13::fixture f(gui, kind);
            hide_all(f, kind, w, 1);
            const record r = s13::settled(f, width, height);
            const entry *row = empty_row(r);
            RV_EDITOR_CHECK(row != nullptr);
            if (row == nullptr) {
                continue;
            }
            const std::string text = row_text(*row);
            const bool ok = has(text, "1 line") && !has(text, "1 lines") && has(text, filter_name(w));
            if (!ok) {
                report((std::string(s13::name_of(kind)) + " one line hidden by " + filter_name(w)).c_str(),
                    "row says '" + text + "'");
            }
            RV_EDITOR_CHECK(ok);
        }
    }
}

// AC8: the button resets that filter and the N lines are shown.
RV_EDITOR_TEST(rv_editor_test_empty_row_log_reset_button_shows_the_lines)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (const auto kind : log_kinds) {
        for (const by w : all_filters) {
            s13::fixture f(gui, kind);
            hide_all(f, kind, w);
            const std::string what = std::string(s13::name_of(kind)) + " reset " + filter_name(w);
            const bool pressed = press_reset(f);
            RV_EDITOR_CHECK(pressed);
            const record r = s13::settled(f, width, height);
            const int rows = log_rows(r);
            if (!pressed || empty_row(r) != nullptr || rows != hidden_count) {
                report(what.c_str(),
                    std::string(pressed ? "pressed" : "no button") + ", " + std::to_string(rows) + " rows, empty row " +
                        (empty_row(r) != nullptr ? "still there" : "gone"));
            }
            RV_EDITOR_CHECK(empty_row(r) == nullptr);
            RV_EDITOR_CHECK(rows == hidden_count);
        }
    }
}

// The first filter that hid the lines is named, and its reset does not touch the next: Source and Find both hide them;
// the row names Source (not Find), the reset shows Find as the reason, its reset shows the lines.
RV_EDITOR_TEST(rv_editor_test_empty_row_log_names_the_first_filter_and_resets_one_at_a_time)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (const auto kind : log_kinds) {
        s13::fixture f(gui, kind);
        hide_all(f, kind, by::source);
        std::snprintf(f.app().outputs[s13::pane].search, sizeof(f.app().outputs[s13::pane].search), "%s", "zzz");
        record r = s13::settled(f, width, height);
        const entry *row = empty_row(r);
        RV_EDITOR_CHECK(row != nullptr);
        const std::string first = row != nullptr ? row_text(*row) : "";
        if (!(has(first, "Source") && !has(first, "Find"))) {
            report(s13::name_of(kind), "with Source and Find hiding, the row says '" + first + "'");
        }
        RV_EDITOR_CHECK(has(first, "Source") && !has(first, "Find"));
        RV_EDITOR_CHECK(press_reset(f));
        r = s13::settled(f, width, height);
        row = empty_row(r);
        const std::string second = row != nullptr ? row_text(*row) : "";
        if (!(has(second, "Find") && has(second, std::to_string(hidden_count).c_str()))) {
            report(s13::name_of(kind), "after Source's reset the row says '" + second + "'");
        }
        RV_EDITOR_CHECK(has(second, "Find") && has(second, std::to_string(hidden_count).c_str()));
        RV_EDITOR_CHECK(press_reset(f));
        r = s13::settled(f, width, height);
        RV_EDITOR_CHECK(empty_row(r) == nullptr && log_rows(r) == hidden_count);
    }
}

// AC8: a log with no lines says there are none and has no button; it is not the filter's row.
RV_EDITOR_TEST(rv_editor_test_empty_row_log_without_lines_says_so_and_has_no_button)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (const auto kind : log_kinds) {
        s13::fixture f(gui, kind);
        const record r = s13::settled(f, width, height);
        const entry *row = empty_row(r);
        RV_EDITOR_CHECK(row != nullptr);
        if (row == nullptr) {
            continue;
        }
        const std::string text = row_text(*row);
        const bool ok = !text.empty() && !has(text, "No lines yet") && !has(text, "Source") && !has(text, "Level") &&
            !has(text, "Find") && !has(text, "Clear View") && row_button(r, *row) == nullptr;
        if (!ok) {
            report(s13::name_of(kind),
                "an empty log's row says '" + text + "', button " + (row_button(r, *row) != nullptr ? "yes" : "no"));
        }
        RV_EDITOR_CHECK(ok);
    }
}

// Lines in sight: no empty row, and "N hidden by the filters" is not a strip (the area keeps the height it has when nothing
// is hidden): the decision is that a log with a visible line says nothing about the rest. Hidden errors and warnings
// are the case the old strip was drawn for.
RV_EDITOR_TEST(rv_editor_test_empty_row_log_with_a_visible_line_has_no_strip_about_hidden_ones)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (const auto kind : log_kinds) {
        const auto visible_only = [](s13::fixture &f) {
            f.app().outputs[s13::pane].show.fill(false);
            f.app().outputs[s13::pane].show[static_cast<size_t>(rv_editor_log_source::editor)] = true;
            f.app().log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "in sight");
        };
        const float plain = area_bottom(gui, kind, visible_only);
        const float with_hidden = area_bottom(gui, kind, [&](s13::fixture &f) {
            visible_only(f);
            f.app().log.add(rv_editor_log_source::runtime, rv_editor_log_level::error, "boom");
            f.app().log.add(rv_editor_log_source::runtime, rv_editor_log_level::warning, "careful");
        });
        if (plain != with_hidden) {
            report(s13::name_of(kind),
                "the area ends at " + std::to_string(plain) + " without hidden lines and " + std::to_string(with_hidden) +
                    " with them");
        }
        RV_EDITOR_CHECK(plain > 0.0f && plain == with_hidden);
        s13::fixture f(gui, kind);
        f.draw(width, height);
        visible_only(f);
        f.app().log.add(rv_editor_log_source::runtime, rv_editor_log_level::error, "boom");
        const record r = s13::settled(f, width, height);
        RV_EDITOR_CHECK(empty_row(r) == nullptr);
        RV_EDITOR_CHECK(log_rows(r) == 1); // the row counter of the reset tests sees a line in sight
    }
}

// The row replaces the strip: lines all hidden (errors and warnings among them) leave the area as tall as a log with no
// lines at all.
RV_EDITOR_TEST(rv_editor_test_empty_row_log_all_hidden_has_no_strip_either)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (const auto kind : log_kinds) {
        const float empty = area_bottom(gui, kind, [](s13::fixture &) {});
        const float hidden = area_bottom(gui, kind, [](s13::fixture &f) {
            f.app().log.add(rv_editor_log_source::runtime, rv_editor_log_level::error, "boom");
            f.app().log.add(rv_editor_log_source::candidate, rv_editor_log_level::warning, "careful");
            f.app().outputs[s13::pane].show.fill(false);
        });
        if (empty != hidden) {
            report(s13::name_of(kind),
                "the area ends at " + std::to_string(empty) + " with no lines and " + std::to_string(hidden) +
                    " with them all hidden");
        }
        RV_EDITOR_CHECK(empty > 0.0f && empty == hidden);
    }
}

// The texts: the old empty line and the old strip text are gone.
RV_EDITOR_TEST(rv_editor_test_empty_row_texts_have_no_no_lines_yet_and_no_hidden_by_the_filters)
{
    const std::string toml = source_of_toml();
    RV_EDITOR_CHECK(toml.find("[pane_output]") != std::string::npos);
    RV_EDITOR_CHECK(toml.find("No lines yet") == std::string::npos);
    RV_EDITOR_CHECK(toml.find("hidden by the filters") == std::string::npos);
}

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
