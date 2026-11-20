// The Inspector Report: a failed or refused build, and the question of a closed project, are one strip under the table
// (text and button in one row, no box, no summary line over the table); the table has three columns, wraps its message,
// has no horizontal bar and takes the pane's whole height; the level icon's tooltip names the level and the source.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iterator>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_pane_tiled.hpp"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_s13.hpp"
#include "ui/rv_editor_widgets_message.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
namespace s13 = rv_editor_test::s13;

namespace
{

constexpr float pane_w = 600.0f;
constexpr float pane_h = 500.0f;
constexpr float eps = 0.5f;

// One draw of the pane: the record, where the pane's body starts and ends, and the strip's own height for `text`.
struct shot {
    rv_editor_probe_record record;
    float top = 0.0f;
    float bottom = 0.0f;
    float strip_h = 0.0f;
    float pad = 0.0f; // the content area's window sits this far inside its frame
};

shot draw(s13::fixture &f, float w = pane_w, float h = pane_h, const std::string &text = "")
{
    shot s;
    for (int i = 0; i < 3; ++i) {
        s.record = rv_editor_probe_draw(
            f.gui,
            w,
            [&](rv_editor_probe_record &) {
                s.top = ImGui::GetCursorScreenPos().y;
                s.bottom = s.top + ImGui::GetContentRegionAvail().y;
                s.pad = ImGui::GetStyle().FramePadding.x;
                // the strip of a pane in the logs' tile says things in one line, a longer text scrolls inside it
                s.strip_h = rv_editor_message_strip_height(text, { "OK" }, ImGui::GetContentRegionAvail().x, false, 1);
                rv_editor_shell_pane(&f.shell, s13::pane, f.kind, stable_tiles::theme);
            },
            h);
    }
    return s;
}

const rv_editor_probe_entry *area_of(const shot &s)
{
    return s13::content_area(s.record);
}

// The entries below the area's frame (its table's rows are inside it, though submitted after): what is under the table.
std::vector<rv_editor_probe_entry> under_area(const shot &s)
{
    const rv_editor_probe_entry *area = area_of(s);
    if (area == nullptr) {
        return {};
    }
    std::vector<rv_editor_probe_entry> under;
    std::copy_if(s.record.entries.begin(), s.record.entries.end(), std::back_inserter(under), [&](const auto &e) {
        return e.y0 >= area->y1 - eps;
    });
    return under;
}

const rv_editor_probe_entry *button(const std::vector<rv_editor_probe_entry> &entries, const std::string &name)
{
    for (const auto &e : entries) {
        if (e.label.substr(0, e.label.find("##")) == name) {
            return &e;
        }
    }
    return nullptr;
}

const ImGuiTable *drawn_table(int columns)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Tables.GetMapSize(); ++i) {
        const ImGuiTable *t = g.Tables.TryGetMapData(i);
        if (t != nullptr && t->ColumnsCount == columns && t->LastFrameActive == g.FrameCount && t->InnerWindow != nullptr) {
            return t;
        }
    }
    return nullptr;
}

// The fixture's state `n`, the project open first (the states build on state 1).
void go(s13::fixture &f, size_t n)
{
    f.states[1]();
    f.states[n]();
}

// A failed build with its number (1) and `reason`, a project open.
void fail_build(s13::fixture &f, const std::string &reason)
{
    go(f, 4); // a build running: it has a number
    f.app().build_alert = { rv_editor_build_alert_kind::failed, reason };
}

} // namespace

// AC6: a failed build is one strip at the bottom of the pane, under the table, with the text and OK in one row.
RV_EDITOR_TEST(rv_editor_test_report_failed_build_is_one_strip_under_the_table)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    const std::string reason = "error: scene.toml:3: unknown key";
    fail_build(f, reason);
    const std::string text = rv_editor_problems_alert_text(f.app());
    RV_EDITOR_CHECK(text == "Build #" + std::to_string(f.app().build.number()) + " failed: " + reason);
    RV_EDITOR_CHECK(f.app().build.number() > 0);
    const shot s = draw(f, pane_w, pane_h, text);
    const auto *area = area_of(s);
    RV_EDITOR_CHECK(area != nullptr);
    RV_EDITOR_CHECK(!s13::has_ask_place(s.record) && s13::message_box(s.record) == nullptr);
    const auto under = under_area(s);
    const auto *ok = button(under, "OK");
    RV_EDITOR_CHECK(!under.empty() && ok != nullptr);
    if (area == nullptr || under.empty() || ok == nullptr) {
        return;
    }
    RV_EDITOR_CHECK(std::count_if(under.begin(), under.end(), [](const auto &e) {
        return e.label.substr(0, e.label.find("##")) == "OK";
    }) == 1);
    float y0 = under.front().y0;
    float y1 = under.front().y1;
    for (const auto &e : under) {
        y0 = std::min(y0, e.y0);
        y1 = std::max(y1, e.y1);
    }
    RV_EDITOR_CHECK(y0 >= area->y1 - eps);                       // under the table, not over it
    RV_EDITOR_CHECK(std::fabs(y1 - s.bottom) < eps);             // at the very bottom of the pane
    RV_EDITOR_CHECK(std::fabs((y1 - y0) - s.strip_h) < eps);     // as high as the text of the build needs, whole
    RV_EDITOR_CHECK(ok->y0 >= y0 - eps && ok->y1 <= y1 + eps);   // OK is in the strip's row
    RV_EDITOR_CHECK(std::fabs(area->y0 - s.top) <= s.pad + eps); // and no summary line is over the table
}

// AC6: OK removes the strip, and the table takes its height back.
RV_EDITOR_TEST(rv_editor_test_report_ok_removes_the_strip)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    fail_build(f, "exit code 1");
    const std::string text = rv_editor_problems_alert_text(f.app());
    shot s = draw(f, pane_w, pane_h, text);
    const auto under = under_area(s);
    const auto *ok = button(under, "OK");
    RV_EDITOR_CHECK(ok != nullptr);
    if (ok == nullptr) {
        return;
    }
    ImGui::GetIO().AddMousePosEvent((ok->x0 + ok->x1) / 2, (ok->y0 + ok->y1) / 2);
    s = draw(f, pane_w, pane_h, text);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    s = draw(f, pane_w, pane_h, text);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    s = draw(f, pane_w, pane_h, text);
    RV_EDITOR_CHECK(f.app().build_alert.kind == rv_editor_build_alert_kind::none);
    s = draw(f);
    RV_EDITOR_CHECK(area_of(s) != nullptr && under_area(s).empty());
    if (area_of(s) != nullptr) {
        RV_EDITOR_CHECK(std::fabs(area_of(s)->y1 - s.bottom) <= s.pad + eps);
    }
}

// AC6: a build that did not start says it in the same strip, with its reason and no "above".
RV_EDITOR_TEST(rv_editor_test_report_refused_build_is_the_same_strip)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    f.states[1]();
    f.app().build_alert = { rv_editor_build_alert_kind::not_started, "no toolchain" };
    const std::string text = rv_editor_problems_alert_text(f.app());
    RV_EDITOR_CHECK(text.find("no toolchain") != std::string::npos);
    RV_EDITOR_CHECK(text.find("above") == std::string::npos);
    const shot s = draw(f, pane_w, pane_h, text);
    const auto under = under_area(s);
    RV_EDITOR_CHECK(button(under, "OK") != nullptr);
    RV_EDITOR_CHECK(area_of(s) != nullptr && std::fabs(area_of(s)->y0 - s.top) <= s.pad + eps);
    f.app().build_alert = {};
    RV_EDITOR_CHECK(rv_editor_problems_alert_text(f.app()).empty());
}

// AC6: no failure and no closed project: no block at all, over or under the table.
RV_EDITOR_TEST(rv_editor_test_report_without_a_message_has_no_block)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    for (const size_t state : { size_t(1), size_t(3), size_t(4) }) { // empty, problems, building
        go(f, state);
        const shot s = draw(f);
        const auto *area = area_of(s);
        RV_EDITOR_CHECK(area != nullptr);
        RV_EDITOR_CHECK(s13::message_box(s.record) == nullptr && !s13::has_ask_place(s.record));
        RV_EDITOR_CHECK(under_area(s).empty());
        if (area != nullptr) {
            RV_EDITOR_CHECK(std::fabs(area->y0 - s.top) <= s.pad + eps);
            RV_EDITOR_CHECK(std::fabs(area->y1 - s.bottom) <= s.pad + eps);
        }
    }
}

// R1: with no project open the question is a strip under the table with its button, nothing over the table.
RV_EDITOR_TEST(rv_editor_test_report_closed_project_asks_in_a_strip)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    f.states[0]();
    const std::string text = rv_editor_text("panes.no_project_open_message");
    const shot s = draw(f, pane_w, pane_h, text);
    const auto under = under_area(s);
    RV_EDITOR_CHECK(button(under, rv_editor_text("panes.open_project_box")) != nullptr);
    RV_EDITOR_CHECK(s13::message_box(s.record) == nullptr);
    RV_EDITOR_CHECK(area_of(s) != nullptr && std::fabs(area_of(s)->y0 - s.top) <= s.pad + eps);
}

// AC6: the texts of the pane say nothing of "above": neither the section of the text file nor the strip's text.
RV_EDITOR_TEST(rv_editor_test_report_texts_do_not_say_above)
{
    s13::load_texts();
    const std::filesystem::path file =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "texts" / "rv_editor_texts.toml";
    std::ifstream in(file);
    std::stringstream all;
    all << in.rdbuf();
    const std::string text = all.str();
    const size_t from = text.find("[pane_problems]");
    RV_EDITOR_CHECK(from != std::string::npos);
    const std::string section = text.substr(from, text.find("\n[", from + 1) - from);
    RV_EDITOR_CHECK(section.find("above") == std::string::npos);
    rv_editor_app app;
    app.build_alert = { rv_editor_build_alert_kind::failed, "exit code 1" };
    const std::string says = rv_editor_problems_alert_text(app);
    RV_EDITOR_CHECK(!says.empty() && says.find("above") == std::string::npos);
}

// AC7: three columns: the level icon, File:line, Message.
RV_EDITOR_TEST(rv_editor_test_report_table_has_three_columns)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    go(f, 3);
    (void)draw(f);
    const ImGuiTable *t = drawn_table(3);
    RV_EDITOR_CHECK(t != nullptr);
    if (t != nullptr) {
        RV_EDITOR_CHECK(std::string(ImGui::TableGetColumnName(t, 1)) == "File:line");
        RV_EDITOR_CHECK(std::string(ImGui::TableGetColumnName(t, 2)) == "Message");
    }
}

// AC7: a 300 character message wraps in its column: no horizontal bar, and its row is taller than one line of text. At
// the pane's minimum width as well as at a wide one.
RV_EDITOR_TEST(rv_editor_test_report_long_message_wraps_and_there_is_no_horizontal_bar)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    f.states[1]();
    f.app().problems.push_back({ f.where / "src/a.cpp", 3, 5, true, s13::words(300), "build" });
    const auto min = stable_tiles::pane_minimum(gui, f.kind);
    float line = 0.0f;
    gui.frame([&] {
        line = ImGui::GetTextLineHeight();
    });
    for (const float w : { pane_w, min.w }) {
        const shot s = draw(f, w, std::max(pane_h, min.h));
        const ImGuiTable *t = drawn_table(3);
        RV_EDITOR_CHECK(t != nullptr);
        if (t != nullptr) {
            RV_EDITOR_CHECK(t->InnerWindow->ScrollMax.x == 0.0f);
        }
        const auto *area = area_of(s);
        RV_EDITOR_CHECK(area != nullptr);
        bool wrapped = false;
        for (const auto &e : s.record.entries) {
            RV_EDITOR_CHECK(!s13::is_area_scroll(e) || ImGui::FindWindowByName(e.label.c_str()) == nullptr ||
                ImGui::FindWindowByName(e.label.c_str())->ScrollMax.x == 0.0f);
            wrapped = wrapped || (area != nullptr && e.y0 >= area->y0 && e.y1 <= area->y1 && e.y1 - e.y0 > 1.5f * line);
        }
        RV_EDITOR_CHECK(wrapped);
    }
}

// AC7: the table reaches the pane's top and bottom (no summary line, no reserved block); with a strip only the bottom rises.
RV_EDITOR_TEST(rv_editor_test_report_table_takes_the_whole_pane)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    go(f, 3);
    const shot s = draw(f);
    const auto *area = area_of(s);
    RV_EDITOR_CHECK(area != nullptr);
    if (area != nullptr) {
        RV_EDITOR_CHECK(std::fabs(area->y0 - s.top) <= s.pad + eps);
        RV_EDITOR_CHECK(std::fabs(area->y1 - s.bottom) <= s.pad + eps);
    }
    fail_build(f, "exit code 1");
    const std::string text = rv_editor_problems_alert_text(f.app());
    const shot with = draw(f, pane_w, pane_h, text);
    RV_EDITOR_CHECK(area_of(with) != nullptr && std::fabs(area_of(with)->y0 - with.top) <= with.pad + eps);
    RV_EDITOR_CHECK(with.strip_h > 0.0f && area_of(with) != nullptr && area_of(with)->y1 < with.bottom - with.strip_h + eps);
}

// AC7: the level icon's tooltip names the level and the source; hovering the icon shows exactly that.
RV_EDITOR_TEST(rv_editor_test_report_icon_tooltip_names_level_and_source)
{
    s13::load_texts();
    const rv_editor_problem err{ "/p/a.cpp", 3, 5, true, "expected ';'", "build" };
    const rv_editor_problem warn{ "/p/b.cpp", 9, 1, false, "unused", "clangd" };
    const std::string e = rv_editor_problems_level_tip(err);
    const std::string w = rv_editor_problems_level_tip(warn);
    RV_EDITOR_CHECK(e.find("error") != std::string::npos && e.find("build") != std::string::npos);
    RV_EDITOR_CHECK(w.find("warning") != std::string::npos && w.find("clangd") != std::string::npos);

    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    f.states[1]();
    f.app().problems.push_back(err);
    shot s = draw(f);
    const ImGuiTable *t = drawn_table(3);
    RV_EDITOR_CHECK(t != nullptr);
    if (t == nullptr) {
        return;
    }
    const ImVec2 mouse(t->Columns[0].WorkMinX + 3.0f, s.top + ImGui::GetFrameHeight() * 1.5f);
    for (int i = 0; i < 40; ++i) {
        ImGui::GetIO().AddMousePosEvent(mouse.x, mouse.y);
        s = draw(f);
    }
    const ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    RV_EDITOR_CHECK(tip != nullptr && tip->WasActive);
    if (tip != nullptr) {
        const float want = ImGui::CalcTextSize(e.c_str()).x + 2.0f * ImGui::GetStyle().WindowPadding.x;
        RV_EDITOR_CHECK(std::fabs(tip->Size.x - want) < 2.5f);
    }
}

// R6: a 200 character relative path does not make the table scroll sideways: the path wraps whole in its column, and
// Message keeps a width of its own.
RV_EDITOR_TEST(rv_editor_test_report_very_long_path_wraps_and_message_keeps_width)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    f.states[1]();
    const std::string path = std::string(100, 'd') + "/" + std::string(98, 'f');
    f.app().problems.push_back({ f.where / path, 3, 5, true, "expected ';'", "build" });
    const shot s = draw(f);
    const ImGuiTable *t = drawn_table(3);
    RV_EDITOR_CHECK(t != nullptr);
    if (t == nullptr) {
        return;
    }
    RV_EDITOR_CHECK(t->InnerWindow->ScrollMax.x == 0.0f);
    RV_EDITOR_CHECK(t->OuterWindow->ScrollMax.x == 0.0f);
    RV_EDITOR_CHECK(t->Columns[2].WorkMaxX - t->Columns[2].WorkMinX > 0.0f);
    // the path is whole, not cut with "...": it wraps, so the row is taller than one line
    float line = 0.0f;
    gui.frame([&] {
        line = ImGui::GetTextLineHeight();
    });
    const auto *area = area_of(s);
    bool wrapped = false;
    for (const auto &e : s.record.entries) {
        wrapped = wrapped || (area != nullptr && e.y0 >= area->y0 && e.y1 <= area->y1 && e.y1 - e.y0 > 1.5f * line);
    }
    RV_EDITOR_CHECK(wrapped);
}

// S13.1a.2: the strip of the Inspector Report is one line high, the 300 character reason of a failed build scrolls inside
// it, is not cut with an ellipsis, and the strip stays the last thing at the pane's bottom.
RV_EDITOR_TEST(rv_editor_test_report_long_alert_is_a_strip_of_one_line_that_scrolls)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    fail_build(f, s13::words(300));
    const std::string text = rv_editor_problems_alert_text(f.app());
    float one = 0.0f;
    gui.frame([&] {
        one = std::max(ImGui::GetFrameHeight(), ImGui::GetTextLineHeight() + 2.0f * ImGui::GetStyle().FramePadding.y);
    });
    for (const float w : { pane_w, stable_tiles::pane_minimum(gui, f.kind).w }) {
        const shot s = draw(f, w, pane_h, text);
        RV_EDITOR_CHECK(std::fabs(s.strip_h - one) < eps);
        const auto *area = area_of(s);
        RV_EDITOR_CHECK(area != nullptr);
        if (area == nullptr) {
            continue;
        }
        float y1 = 0.0f;
        float y0 = 1.0e9f;
        for (const auto &e : under_area(s)) {
            if (e.label.substr(0, e.label.find("##")) == "OK") {
                y0 = std::min(y0, e.y0);
                y1 = std::max(y1, e.y1);
            }
        }
        RV_EDITOR_CHECK(y1 > 0.0f && std::fabs(y1 - s.bottom) < eps && y1 - y0 <= one + eps);
        for (const auto &e : s.record.entries) {
            RV_EDITOR_CHECK(e.label.substr(0, e.label.find("##")).find("...") == std::string::npos);
        }
    }
}

// S13.1a.2: the minimum of the Inspector Report is the area at its floor, the strip of one line and the gap over it, the
// same in every state (the strip's own height is the most it ever takes), and a failed build with a long reason, a closed
// project and a plain table are each whole at it.
RV_EDITOR_TEST(rv_editor_test_report_minimum_is_the_floor_and_a_strip_of_one_line)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    const auto min = stable_tiles::pane_minimum(gui, f.kind);
    float want = 0.0f;
    gui.frame([&] {
        const ImGuiStyle &style = ImGui::GetStyle();
        const float one = std::max(ImGui::GetFrameHeight(), ImGui::GetTextLineHeight() + 2.0f * style.FramePadding.y);
        want = static_cast<float>(rv_editor_tiled_minimum(100.0f, 0.0f).h) + one + style.ItemSpacing.y;
    });
    if (std::fabs(min.h - want) > 1.5f) {
        std::fprintf(stderr,
            "  problems: the minimum is %.0f px high; the floor, one strip line and a gap make %.0f\n",
            min.h,
            want);
    }
    RV_EDITOR_CHECK(std::fabs(min.h - want) <= 1.5f);
    const std::vector<std::function<void()>> states = {
        [&] {
            go(f, 3);
        },
        [&] {
            fail_build(f, s13::words(300));
        },
        [&] {
            go(f, 1);
            f.app().project.open = false;
        },
    };
    for (size_t i = 0; i < states.size(); ++i) {
        const std::string bad = rv_editor_check_pane_states(gui, f.shell, f.kind, { states[i] }, min);
        if (!bad.empty()) {
            std::fprintf(stderr, "  problems state %zu at the minimum: %s\n", i, bad.c_str());
        }
        RV_EDITOR_CHECK(bad.empty());
    }
}
