// S19, R3/R9/R19, AC1/AC11: Open Project is a form of the path and the directory list in a content area under it, with
// the answer buttons under the area; Help is the filter over a table in a content area; the manual is its text in a content
// area. In none of them does the tile's body scroll: only the area does.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "rv_editor_test.hpp"
#include "rv_editor_test_content.hpp"
#include "rv_editor_test_s19_check.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s19;

namespace
{

constexpr size_t help_all = 0;
constexpr size_t help_some = 1;
constexpr size_t help_none = 2;

// Content areas draw Motif bars (ImGui window bars are off there), so a bar is a ##track item.
bool has_bar(const rv_editor_probe_record &r)
{
    const rv_editor_test::content::bars b = rv_editor_test::content::bars_of(r);
    return b.v || b.h;
}

// An entry outside scroll areas whose label, in lower case, has `part`.
const rv_editor_probe_entry *field_like(const rv_editor_probe_record &r, std::string part)
{
    for (const auto &e : r.entries) {
        std::string label = e.label;
        std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (!e.in_scroll && label.find("##") != std::string::npos && label.find(part) != std::string::npos) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

// Open Project: the directory list is in the content area (all of it, in a tall tile), the area is a frame of its own, the
// path field and the status are above it and the answer buttons under it in a row.
RV_EDITOR_TEST(rv_editor_test_s19_open_project_lists_the_directory_in_the_content_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::open_project);
    const minimum min = minimum_of(gui, view::open_project);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    for (size_t i = 0; i < f.states.size(); ++i) {
        f.states[i].set();
        const auto r = f.settled(std::max(min.w, 600.0f), 3000.0f);
        const rv_editor_probe_entry *area = content_area(r);
        const rv_editor_probe_entry *cancel = item_named(r, rv_editor_text("browser.cancel"));
        const rv_editor_probe_entry *open = item_named(r, "Open Project");
        if (area == nullptr || cancel == nullptr || open == nullptr) {
            std::fprintf(stderr,
                "  open_project state %zu: area %s, Cancel %s, Open Project %s\n",
                i,
                area != nullptr ? "there" : "missing",
                cancel != nullptr ? "there" : "missing",
                open != nullptr ? "there" : "missing");
        }
        RV_EDITOR_CHECK(area != nullptr && cancel != nullptr && open != nullptr);
        if (area == nullptr || cancel == nullptr || open == nullptr) {
            continue;
        }
        RV_EDITOR_CHECK(cancel->y0 >= area->y1 - 0.5f && open->y0 == cancel->y0 && open->x1 <= cancel->x0);
        if (i < 2) {
            RV_EDITOR_CHECK(items_in_scroll(r) >= static_cast<size_t>(dirs_in_where));
        }
    }
}

// Open Project: the path field is above the area; its value is the browser's directory, whole (S6).
RV_EDITOR_TEST(rv_editor_test_s19_open_project_path_field_is_above_the_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::open_project);
    f.states[0].set();
    const auto r = f.settled(800.0f, 600.0f);
    const rv_editor_probe_entry *area = content_area(r);
    const rv_editor_probe_entry *path = item_with(r, "##path");
    path = path != nullptr ? path : field_like(r, "path");
    RV_EDITOR_CHECK(area != nullptr && path != nullptr);
    RV_EDITOR_CHECK(area != nullptr && path != nullptr && path->y1 <= area->y0 + 0.5f);
    RV_EDITOR_CHECK(rv_editor_browser_path_probe().shown == f.shell.open_browser.dir.string());
}

// Help: the filter is above the table and the table is in the content area; a filter that keeps fewer rows leaves fewer
// rows in the area, and the area stays where it was.
RV_EDITOR_TEST(rv_editor_test_s19_help_table_is_in_the_content_area_under_the_filter)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::help);
    const minimum min = minimum_of(gui, view::help);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    const float w = std::max(min.w, 600.0f);
    f.states[help_all].set();
    const auto all = f.settled(w, 3000.0f);
    f.states[help_some].set();
    const auto some = f.settled(w, 3000.0f);
    f.states[help_none].set();
    const auto none = f.settled(w, 3000.0f);
    const rv_editor_probe_entry *area = content_area(all);
    const rv_editor_probe_entry *filter = field_like(all, "filter");
    RV_EDITOR_CHECK(area != nullptr && filter != nullptr);
    RV_EDITOR_CHECK(area != nullptr && filter != nullptr && filter->y1 <= area->y0 + 0.5f);
    RV_EDITOR_CHECK(items_in_scroll(all) > items_in_scroll(some));
    RV_EDITOR_CHECK(items_in_scroll(some) > 0);
    RV_EDITOR_CHECK(content_area(some) != nullptr && content_area(none) != nullptr);
    RV_EDITOR_CHECK(s13::windows_difference(all, some).empty());
    RV_EDITOR_CHECK(s13::windows_difference(all, none).empty());
    // The filter stays at its place whatever it holds.
    RV_EDITOR_CHECK(field_like(none, "filter") != nullptr && moved(all, none).empty());
}

// Manual: the text is in the content area, which at the minimum scrolls (the manual is long); nothing but the title is
// outside it, and no text ends in "...".
RV_EDITOR_TEST(rv_editor_test_s19_manual_text_is_in_the_content_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::manual);
    const minimum min = minimum_of(gui, view::manual);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    const auto r = f.settled(min.w, min.h);
    RV_EDITOR_CHECK(content_area(r) != nullptr);
    RV_EDITOR_CHECK(items_in_scroll(r) > 3);
    RV_EDITOR_CHECK(has_bar(r));
    for (const auto &e : r.entries) {
        RV_EDITOR_CHECK(!contains(e.label.substr(0, e.label.find("##")), "..."));
    }
}
