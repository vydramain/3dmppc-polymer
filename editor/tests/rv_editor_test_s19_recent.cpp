// S19, R10/R20, AC11: every record of Recent Projects is exactly two lines (name, path) whatever the length of either, and
// its status is a pill that is whole, at the right edge of the visible area and never wraps by letters.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "rv_editor_test.hpp"
#include "rv_editor_test_content.hpp"
#include "rv_editor_test_s19_check.hpp"
#include "ui/rv_editor_widgets_content.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s19;

namespace
{

constexpr size_t state_records = 1;
constexpr size_t state_long = 2;
constexpr size_t state_missing = 3;

struct record_view {
    const rv_editor_probe_entry *row = nullptr;  // the selectable of the record ("##row")
    const rv_editor_probe_entry *pill = nullptr; // the item at the record's right end
    std::set<int> lines;                         // distinct tops of the items inside the record
    float bottom = 0.0f;                         // lowest edge of an item inside the record
};

// The records of a drawn Recent Projects page, top to bottom.
std::vector<record_view> records_of(const rv_editor_probe_record &r)
{
    std::vector<record_view> out;
    for (const auto &e : r.entries) {
        if (e.in_scroll && contains(e.label, "##row")) {
            out.push_back({ &e });
        }
    }
    for (auto &v : out) {
        std::vector<const rv_editor_probe_entry *> inside;
        for (const auto &e : r.entries) {
            const float cy = (e.y0 + e.y1) / 2.0f;
            const bool item = e.in_scroll && &e != v.row && !contains(e.label, "##") && !is_window(e) && e.x1 - e.x0 >= 1.0f &&
                e.y1 - e.y0 >= 1.0f;
            if (item && cy >= v.row->y0 && cy <= v.row->y1) {
                inside.push_back(&e);
                v.bottom = std::max(v.bottom, e.y1);
                if (v.pill == nullptr || e.x1 > v.pill->x1) {
                    v.pill = &e;
                }
            }
        }
        // The pill is the right end; the other items are the name and the path.
        for (const auto *e : inside) {
            if (e != v.pill) {
                v.lines.insert(static_cast<int>(e->y0 + 0.5f));
            }
        }
    }
    return out;
}

struct measures {
    float line = 0.0f, spaced = 0.0f, status = 0.0f;
};

measures measure(rv_editor_test_imgui &gui)
{
    measures m;
    gui.frame([&] {
        m.line = ImGui::GetTextLineHeight();
        m.spaced = ImGui::GetTextLineHeightWithSpacing();
        const float a = ImGui::CalcTextSize(rv_editor_text("shell_start.status_available")).x;
        const float b = ImGui::CalcTextSize(rv_editor_text("shell_start.status_missing")).x;
        m.status = std::max(a, b);
    });
    return m;
}

// Draws state `i` at `width` x `height`; the area frame is returned through `area`.
rv_editor_probe_record at_state(fixture &f, size_t i, float width, float height)
{
    f.states[i].set();
    return f.settled(width, height);
}

} // namespace

// A record is two lines: its height is that of two text lines and what is inside it has two tops at most, whether its name
// and path are short, 200 characters long or hundreds more.
RV_EDITOR_TEST(rv_editor_test_s19_recent_records_are_two_lines_whatever_the_length)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::recent);
    const minimum min = minimum_of(gui, view::recent);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    const measures m = measure(gui);
    float height_of_short = 0.0f;
    for (const size_t state : { state_records, state_long, state_missing }) {
        for (const float width : { min.w, min.w * 2.0f }) {
            const auto r = at_state(f, state, width, min.h * 1.5f);
            const auto records = records_of(r);
            if (records.size() != 3) {
                std::fprintf(stderr, "  state %zu width %.0f: %zu records, expected 3\n", state, width, records.size());
            }
            RV_EDITOR_CHECK(records.size() == 3);
            for (const auto &v : records) {
                const float h = v.row->y1 - v.row->y0;
                if (h < 2.0f * m.line - 0.5f || h >= 3.0f * m.spaced || v.lines.size() > 2) {
                    std::fprintf(stderr,
                        "  state %zu width %.0f: a record is %.1f high with %zu line tops (a line is %.1f)\n",
                        state,
                        width,
                        h,
                        v.lines.size(),
                        m.line);
                }
                RV_EDITOR_CHECK(h >= 2.0f * m.line - 0.5f && h < 3.0f * m.spaced);
                RV_EDITOR_CHECK(v.lines.size() == 2 && v.bottom <= v.row->y1 + 0.5f);
                height_of_short = height_of_short == 0.0f ? h : height_of_short;
                RV_EDITOR_CHECK(std::abs(h - height_of_short) < 0.5f);
            }
        }
    }
}

// The status: a pill as wide as the longest status word, whole, with its right edge at the visible right edge of the area
// (not past it), the same in every record, one line high; a narrow tile does not make it wrap by letters.
RV_EDITOR_TEST(rv_editor_test_s19_recent_status_is_whole_and_pinned_at_the_right_edge)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::recent);
    const minimum min = minimum_of(gui, view::recent);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    const measures m = measure(gui);
    for (const size_t state : { state_records, state_long, state_missing }) {
        for (const float width : { min.w, min.w * 2.0f }) {
            const auto r = at_state(f, state, width, min.h * 1.5f);
            const rv_editor_probe_entry *area = content_area(r);
            RV_EDITOR_CHECK(area != nullptr);
            const auto records = records_of(r);
            RV_EDITOR_CHECK(records.size() == 3);
            for (const auto &v : records) {
                RV_EDITOR_CHECK(v.pill != nullptr);
                if (v.pill == nullptr || area == nullptr) {
                    continue;
                }
                const float pill_w = v.pill->x1 - v.pill->x0;
                if (pill_w < m.status || v.pill->x1 > area->x1 + 0.5f || v.pill->y1 - v.pill->y0 > m.spaced) {
                    std::fprintf(stderr,
                        "  state %zu width %.0f: pill %.1f wide, %.1f high, right edge %.1f, area edge %.1f (word %.1f)\n",
                        state,
                        width,
                        pill_w,
                        v.pill->y1 - v.pill->y0,
                        v.pill->x1,
                        area->x1,
                        m.status);
                }
                RV_EDITOR_CHECK(pill_w >= m.status);
                RV_EDITOR_CHECK(v.pill->x1 <= area->x1 + 0.5f);
                RV_EDITOR_CHECK(v.pill->y1 - v.pill->y0 <= m.spaced);
                RV_EDITOR_CHECK(std::abs(v.pill->x1 - records[0].pill->x1) < 0.5f);
            }
        }
    }
}

// The records are in the content area and show no "..." (R2): the name and path of a long record are whole, the area
// scrolls for them.
RV_EDITOR_TEST(rv_editor_test_s19_recent_long_records_are_in_the_content_area_with_no_ellipsis)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::recent);
    const minimum min = minimum_of(gui, view::recent);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    const auto r = at_state(f, state_long, min.w, min.h);
    const rv_editor_probe_entry *area = content_area(r);
    RV_EDITOR_CHECK(area != nullptr);
    const auto records = records_of(r);
    RV_EDITOR_CHECK(records.size() == 3);
    for (const auto &v : records) {
        RV_EDITOR_CHECK(area != nullptr && v.row->y0 >= area->y0 - 0.5f && v.row->y1 <= area->y1 + 0.5f);
    }
    for (const auto &e : r.entries) {
        RV_EDITOR_CHECK(!contains(e.label.substr(0, e.label.find("##")), "..."));
    }
    // A record wider than the area: the area shows a horizontal bar and no more.
    RV_EDITOR_CHECK(rv_editor_test::content::bars_of(r).h);
}

// Empty: the area stays with a line that says what will be here; no record.
RV_EDITOR_TEST(rv_editor_test_s19_recent_empty_keeps_the_area_and_has_no_record)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, view::recent);
    const minimum min = minimum_of(gui, view::recent);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    const auto r = at_state(f, 0, min.w, min.h);
    RV_EDITOR_CHECK(content_area(r) != nullptr);
    RV_EDITOR_CHECK(records_of(r).empty());
    RV_EDITOR_CHECK(items_in_scroll(r) > 0); // the dim "No recent projects" line, inside the area
}
