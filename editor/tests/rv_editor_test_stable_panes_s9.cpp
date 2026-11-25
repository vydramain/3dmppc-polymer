// Output, Terminal, Problems and Search keep every block, button and message line in its place whatever the state says:
// two states drawn at the same width have the same rectangles; a path cell shows the path's end with an ellipsis.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "app/rv_editor_app.hpp"
#include "app/rv_editor_shell.hpp"
#include "build/rv_editor_build_alert.hpp"
#include "font/rv_editor_font.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_s3.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_strip.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr rv_editor_pane_id pane = 1;
constexpr float output_width = 520.0f;
constexpr float narrow_width = 360.0f;
constexpr int settle_frames = 3;

void setup()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

// The last of a few frames of `body`, so what depends on the previous frame has settled.
template <class F>
rv_editor_probe_record settled(rv_editor_test_imgui &gui, float width, F body)
{
    rv_editor_probe_record last;
    for (int i = 0; i < settle_frames; ++i) {
        last = rv_editor_probe_draw(gui, width, [&](auto &) {
            body();
        });
    }
    return last;
}

void same(const char *what, const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    const std::string difference = rv_editor_probe_compare(a, b);
    if (!difference.empty()) {
        std::fprintf(stderr, "%s: %s\n", what, difference.c_str());
    }
    RV_EDITOR_CHECK(difference.empty());
}

// The record without what moves legally with the state: the items inside a content area, the message strip (it comes and
// goes and is as tall as its text) and the bottom edge of the areas and the old box's window (a strip takes the area's
// height at its bottom). The windows' tops, sides and the area frames' places stay.
rv_editor_probe_record frames_only(const rv_editor_probe_record &r)
{
    const rv_editor_probe_entry *strip = rv_editor_test::s3::strip_of(r);
    // A box left over from a pane not yet converted: its text window and the button row under it.
    const rv_editor_probe_entry *box = nullptr;
    for (const auto &e : r.entries) {
        box =
            e.scroll_area && rv_editor_test::s13::is_child(rv_editor_test::s13::parts_of(e.label).back(), "##text") ? &e : box;
    }
    const float row = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    rv_editor_probe_record out;
    for (const auto &e : r.entries) {
        const bool in_strip = strip != nullptr && e.y0 >= strip->y0 - 0.5f && e.y1 <= strip->y1 + 0.5f;
        const bool in_box = box != nullptr && &e != box && e.y0 >= box->y0 && e.y1 <= box->y1 + row + 0.5f;
        if (e.in_scroll || in_strip || in_box) {
            continue;
        }
        out.entries.push_back(e);
        if (e.scroll_area || e.label.find("##area") != std::string::npos) {
            out.entries.back().y1 = e.y0;
        }
    }
    return out;
}

// For the panes that say things in a strip under their area (the logs, Terminal, Search): the two draws agree on everything
// above the shorter area's bottom edge, where the area's own frame is cut at the edge; the strip and its gap are what may
// differ, and the items inside the areas.
void same_above_strip(const char *what, const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    const auto *area_a = strip::last_area(a);
    const auto *area_b = strip::last_area(b);
    float cut = 1.0e9f;
    cut = area_a != nullptr ? std::min(cut, area_a->y1) : cut;
    cut = area_b != nullptr ? std::min(cut, area_b->y1) : cut;
    const auto above = [cut](const rv_editor_probe_record &r) {
        rv_editor_probe_record out;
        for (auto e : r.entries) {
            if (e.in_scroll || e.y0 >= cut - 0.5f) {
                continue;
            }
            e.y1 = e.y1 > cut - 0.5f ? 0.0f : e.y1;
            out.entries.push_back(e);
        }
        return out;
    };
    same(what, above(a), above(b));
}

std::string long_text(size_t size)
{
    std::string s;
    while (s.size() < size) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

const rv_editor_probe_entry *find_suffix(const rv_editor_probe_record &r, const std::string &suffix)
{
    for (const auto &e : r.entries) {
        if (e.label.size() >= suffix.size() && e.label.compare(e.label.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return &e;
        }
    }
    return nullptr;
}

// 120 characters under the project root, ending in a file name.
std::string long_relative()
{
    std::string path = "src/deeply_nested_dir/";
    while (path.size() < 109) {
        path += "deeply_nested_dir/";
    }
    path.resize(109);
    return path + "/engine.cfg";
}

constexpr const char *root_dir = "/home/user/proj";

// --- Output ------------------------------------------------------------------------------------------------------

rv_editor_probe_record draw_output(rv_editor_test_imgui &gui, rv_editor_app &app)
{
    return settled(gui, output_width, [&] {
        rv_editor_pane_output(app, pane, theme);
    });
}

void add_info(rv_editor_app &app, int count)
{
    for (int i = 0; i < count; ++i) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "line");
    }
}

// --- Terminal ----------------------------------------------------------------------------------------------------

struct terminal_state {
    rv_editor_app app;
    rv_editor_ui ui;
    std::vector<rv_editor_ui_request> requests;
};

rv_editor_probe_record draw_terminal(rv_editor_test_imgui &gui, terminal_state &t)
{
    return settled(gui, narrow_width, [&] {
        rv_editor_pane_terminal(t.app, pane, theme, rv_editor_ui_port{ t.ui, t.requests });
    });
}

// The shell of the terminal at `pane` has ended (it was started as `shell`, which exits at once).
bool wait_ended(rv_editor_test_imgui &gui, terminal_state &t)
{
    for (int i = 0; i < 150; ++i) {
        draw_terminal(gui, t);
        const auto &view = t.app.terminals[pane];
        std::string error;
        if (view.term != nullptr) {
            (void)view.term->update(error);
        }
        if (view.term != nullptr && !view.term->running()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

// --- Problems and Search -----------------------------------------------------------------------------------------

void problems_app(rv_editor_app &app)
{
    app.project.open = true;
    app.project.root = root_dir;
    app.problems.push_back({ std::filesystem::path(root_dir) / "src/main.cpp", 3, 5, true, "expected ';'", "build" });
    app.problems.push_back({ std::filesystem::path(root_dir) / "src/util.cpp", 9, 1, false, "unused variable", "build" });
}

rv_editor_probe_record draw_problems(rv_editor_test_imgui &gui, rv_editor_app &app)
{
    return settled(gui, narrow_width, [&] {
        rv_editor_pane_problems(app, theme);
    });
}

void search_app(rv_editor_app &app, size_t hits)
{
    app.project.open = true;
    app.project.root = root_dir;
    app.project_search.searched = "needle";
    for (size_t i = 0; i < hits; ++i) {
        app.project_search.result.hits.push_back(
            { std::filesystem::path(root_dir) / "src/a.cpp", static_cast<int32_t>(i + 1), 4, "int needle = 0;" });
    }
    app.project_search.result.files = 12;
}

rv_editor_probe_record draw_search(rv_editor_test_imgui &gui, rv_editor_app &app)
{
    return settled(gui, narrow_width, [&] {
        rv_editor_pane_search(app, theme);
    });
}

// The path cell's drawn text (the row's label up to its id marker); empty when there is none.
std::string row_text(const rv_editor_probe_record &r)
{
    const rv_editor_probe_entry *row = find_suffix(r, "###row");
    return row == nullptr ? "" : row->label.substr(0, row->label.size() - 6);
}

// Hover the left edge of the row's visible part for a few frames; true when a tooltip is up.
template <class F>
bool tooltip_on_row(rv_editor_test_imgui &gui, float width, F body)
{
    ImVec2 mouse(1.0f, 1.0f);
    for (int i = 0; i < 4; ++i) {
        ImGui::GetIO().MousePos = mouse;
        const auto r = rv_editor_probe_draw(gui, width, [&](auto &) {
            body();
        });
        if (const auto *row = find_suffix(r, "###row")) {
            mouse = ImVec2(row->x0 + 4.0f, (row->y0 + row->y1) / 2.0f);
        }
    }
    const ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    return tip != nullptr && tip->WasActive;
}

} // namespace

// --- Output ------------------------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_stable_s9_output_no_lines_note)
{
    rv_editor_test_imgui gui(true);
    setup();
    // A run with lines (hidden by Clear View so nothing is shown) versus a run that has none: only the note differs.
    rv_editor_app with_lines;
    add_info(with_lines, 2);
    with_lines.log.add(rv_editor_log_source::build, rv_editor_log_level::info, "x", rv_editor_log_channel::out, 7, 1);
    with_lines.outputs[pane].run_pid = 7;
    with_lines.outputs[pane].run_label = "Build #1, pid 7";
    with_lines.outputs[pane].hide_before = with_lines.log.revision() + 1;
    rv_editor_app none;
    none.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "x");
    none.outputs[pane].run_pid = 7;
    none.outputs[pane].run_label = "Build #1, pid 7";
    none.outputs[pane].hide_before = none.log.revision() + 1;
    same_above_strip("run without lines", draw_output(gui, with_lines), draw_output(gui, none));
}

// S4: lines hidden by a filter are not a note under a log that shows lines: the draw with hidden errors and warnings is the
// draw without them, the strip included (the area ends at the same pixel).
RV_EDITOR_TEST(rv_editor_test_stable_s9_output_hidden_lines_are_no_note_while_one_is_shown)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app plain;
    rv_editor_app hidden;
    for (rv_editor_app *app : { &plain, &hidden }) {
        app->outputs[pane].show[static_cast<size_t>(rv_editor_log_source::runtime)] = false;
    }
    hidden.log.add(rv_editor_log_source::runtime, rv_editor_log_level::error, "boom");
    hidden.log.add(rv_editor_log_source::runtime, rv_editor_log_level::warning, "careful");
    add_info(plain, 3);
    add_info(hidden, 3);
    const auto a = draw_output(gui, plain);
    const auto b = draw_output(gui, hidden);
    same_above_strip("hidden errors and warnings", a, b);
    const auto *area_a = strip::last_area(a);
    const auto *area_b = strip::last_area(b);
    RV_EDITOR_CHECK(area_a != nullptr && area_b != nullptr);
    RV_EDITOR_CHECK(area_a != nullptr && area_b != nullptr && area_a->y1 == area_b->y1);
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_output_dropped_note)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app kept;
    rv_editor_app dropped;
    for (size_t i = 0; i < rv_editor_log::capacity; ++i) {
        kept.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "x");
        dropped.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "x");
    }
    dropped.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "x");
    RV_EDITOR_CHECK(kept.log.dropped() == 0 && dropped.log.dropped() != 0);
    // Only the last five lines are shown, in both.
    kept.outputs[pane].hide_before = kept.log.revision() - 4;
    dropped.outputs[pane].hide_before = dropped.log.revision() - 4;
    same_above_strip("dropped lines", draw_output(gui, kept), draw_output(gui, dropped));
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_output_exported_note)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app plain;
    rv_editor_app exported;
    add_info(plain, 3);
    add_info(exported, 3);
    exported.outputs[pane].exported = std::string(root_dir) + "/" + long_relative();
    same_above_strip("last export", draw_output(gui, plain), draw_output(gui, exported));
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_output_source_label)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app all;
    rv_editor_app run;
    all.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "x");
    run.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "x");
    run.log.add(rv_editor_log_source::build, rv_editor_log_level::info, "x", rv_editor_log_channel::out, 7, 1);
    run.outputs[pane].run_pid = 7;
    run.outputs[pane].run_label = "Build run #12, pid 7 " + long_text(100);
    run.outputs[pane].hide_before = run.log.revision() + 1;
    all.outputs[pane].hide_before = all.log.revision() + 1;
    same("Source label short and long", draw_output(gui, all), draw_output(gui, run));
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_output_level_label)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app all;
    rv_editor_app warnings;
    rv_editor_app errors;
    warnings.outputs[pane].level = rv_editor_log_level::warning;
    errors.outputs[pane].level = rv_editor_log_level::error;
    const auto a = draw_output(gui, all);
    same("Level All and Warnings", a, draw_output(gui, warnings));
    same("Level All and Errors", a, draw_output(gui, errors));
}

// --- Terminal ----------------------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_stable_s9_terminal_shell_alive_and_ended)
{
    rv_editor_test_imgui gui(true);
    setup();
    terminal_state alive;
    terminal_state ended;
    setenv("SHELL", "/bin/sh", 1);
    const auto a = draw_terminal(gui, alive);
    RV_EDITOR_CHECK(alive.app.terminals[pane].term != nullptr && alive.app.terminals[pane].term->running());
    setenv("SHELL", "/bin/true", 1);
    RV_EDITOR_CHECK(wait_ended(gui, ended));
    same_above_strip("shell alive and ended", a, draw_terminal(gui, ended));
    setenv("SHELL", "/bin/sh", 1);
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_terminal_paste_ask)
{
    rv_editor_test_imgui gui(true);
    setup();
    setenv("SHELL", "/bin/sh", 1);
    terminal_state t;
    const auto closed = draw_terminal(gui, t);
    RV_EDITOR_CHECK(t.app.terminals[pane].term != nullptr);
    t.app.terminals[pane].paste = "first line\nsecond line " + long_text(300) + "\nthird line";
    same_above_strip("paste ask closed and open", closed, draw_terminal(gui, t));
}

// --- Problems ----------------------------------------------------------------------------------------------------

// A build alert, refused or failed, short or long, is a strip under the table: the table's first row and the area's top
// stay where they were and the area loses height only at its bottom.
RV_EDITOR_TEST(rv_editor_test_stable_s9_problems_build_alert)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app plain;
    problems_app(plain);
    const auto base = draw_problems(gui, plain);
    const auto *base_row = find_suffix(base, "###row");
    const auto *base_area = s13::content_area(base);
    RV_EDITOR_CHECK(base_row != nullptr && base_area != nullptr);
    for (const auto kind : { rv_editor_build_alert_kind::not_started, rv_editor_build_alert_kind::failed }) {
        for (const std::string &reason : { std::string("exit code 1"), long_text(300) }) {
            rv_editor_app alert;
            problems_app(alert);
            alert.build_alert = { kind, reason };
            const auto r = draw_problems(gui, alert);
            const auto *row = find_suffix(r, "###row");
            const auto *area = s13::content_area(r);
            RV_EDITOR_CHECK(row != nullptr && area != nullptr);
            if (row == nullptr || area == nullptr || base_row == nullptr || base_area == nullptr) {
                continue;
            }
            RV_EDITOR_CHECK(row->y0 == base_row->y0 && row->y1 == base_row->y1);
            RV_EDITOR_CHECK(area->y0 == base_area->y0 && area->x0 == base_area->x0 && area->x1 == base_area->x1);
            RV_EDITOR_CHECK(area->y1 < base_area->y1);
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_problems_path_cell_shows_the_whole_relative_path)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app app;
    problems_app(app);
    app.problems[0].file = std::filesystem::path(root_dir) / long_relative();
    const auto r = draw_problems(gui, app);
    const std::string shown = row_text(r);
    RV_EDITOR_CHECK(!shown.empty());
    RV_EDITOR_CHECK(shown == long_relative() + ":3"); // whole, relative to the project root, with its line, no "..."
    RV_EDITOR_CHECK(shown.find("...") == std::string::npos);
    RV_EDITOR_CHECK(tooltip_on_row(gui, narrow_width, [&] {
        rv_editor_pane_problems(app, theme);
    }));
}

// --- Search ------------------------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_stable_s9_search_result_notes)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app plain;
    rv_editor_app noted;
    search_app(plain, 3);
    search_app(noted, 3);
    noted.project_search.result.truncated = true;
    noted.project_search.result.skipped_dirs = 4;
    noted.project_search.result.skipped_files = 9;
    noted.project.root = "/home/user/a_project_with_a_rather_long_folder_name";
    // The File column fits its content, so the path stays the same relative path under the longer root.
    noted.project_search.result.hits.clear();
    for (size_t i = 0; i < 3; ++i) {
        noted.project_search.result.hits.push_back(
            { std::filesystem::path(noted.project.root) / "src/a.cpp", static_cast<int32_t>(i + 1), 4, "int needle = 0;" });
    }
    same_above_strip("results with the scope and stopped notes", draw_search(gui, plain), draw_search(gui, noted));
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_search_path_cell_shows_the_whole_relative_path)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app app;
    search_app(app, 1);
    app.project_search.result.hits[0].file = std::filesystem::path(root_dir) / long_relative();
    const auto r = draw_search(gui, app);
    const std::string shown = row_text(r);
    RV_EDITOR_CHECK(!shown.empty());
    RV_EDITOR_CHECK(shown == long_relative()); // whole, relative to the project root, no "..."
    RV_EDITOR_CHECK(shown.find("...") == std::string::npos);
    RV_EDITOR_CHECK(tooltip_on_row(gui, narrow_width, [&] {
        rv_editor_pane_search(app, theme);
    }));
}
