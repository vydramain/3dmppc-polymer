// Output, Terminal, Problems and Search keep every block, button and message line in its place whatever the state says:
// two states drawn at the same width have the same rectangles; a path cell shows the path's end with an ellipsis.

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
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
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

bool ends_with(const std::string &s, const std::string &tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// Hover the centre of the row for a few frames; true when a tooltip is up.
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
            mouse = ImVec2((row->x0 + row->x1) / 2.0f, (row->y0 + row->y1) / 2.0f);
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
    same("run without lines", draw_output(gui, with_lines), draw_output(gui, none));
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_output_hidden_counts_note)
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
    same("hidden errors and warnings", draw_output(gui, plain), draw_output(gui, hidden));
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
    same("dropped lines", draw_output(gui, kept), draw_output(gui, dropped));
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
    same("last export", draw_output(gui, plain), draw_output(gui, exported));
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
    same("shell alive and ended", a, draw_terminal(gui, ended));
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
    same("paste ask closed and open", closed, draw_terminal(gui, t));
}

// --- Problems ----------------------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_stable_s9_problems_build_alert)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app plain;
    problems_app(plain);
    const auto base = draw_problems(gui, plain);
    for (const auto kind : { rv_editor_build_alert_kind::not_started, rv_editor_build_alert_kind::failed }) {
        for (const std::string &reason : { std::string("exit code 1"), long_text(300) }) {
            rv_editor_app alert;
            problems_app(alert);
            alert.build_alert = { kind, reason };
            same(kind == rv_editor_build_alert_kind::failed ? "build failed alert" : "build not started alert",
                base,
                draw_problems(gui, alert));
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_problems_path_cell_keeps_the_end)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app app;
    problems_app(app);
    app.problems[0].file = std::filesystem::path(root_dir) / long_relative();
    const auto r = draw_problems(gui, app);
    const std::string shown = row_text(r);
    RV_EDITOR_CHECK(!shown.empty());
    RV_EDITOR_CHECK(shown.compare(0, 3, "...") == 0);
    RV_EDITOR_CHECK(ends_with(shown, "engine.cfg"));
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
    same("results with the scope and stopped notes", draw_search(gui, plain), draw_search(gui, noted));
}

RV_EDITOR_TEST(rv_editor_test_stable_s9_search_path_cell_keeps_the_end)
{
    rv_editor_test_imgui gui(true);
    setup();
    rv_editor_app app;
    search_app(app, 1);
    app.project_search.result.hits[0].file = std::filesystem::path(root_dir) / long_relative();
    const auto r = draw_search(gui, app);
    const std::string shown = row_text(r);
    RV_EDITOR_CHECK(!shown.empty());
    RV_EDITOR_CHECK(shown.compare(0, 3, "...") == 0);
    RV_EDITOR_CHECK(ends_with(shown, "engine.cfg"));
    RV_EDITOR_CHECK(tooltip_on_row(gui, narrow_width, [&] {
        rv_editor_pane_search(app, theme);
    }));
}
