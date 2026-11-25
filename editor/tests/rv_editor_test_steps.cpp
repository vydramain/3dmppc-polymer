// The step strip: marks per state, the compact text, the cells of the full form and its bar.

#include <string>

#include "build/rv_editor_progress.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "ui/rv_editor_widgets_steps.hpp"

using namespace rv_editor;

namespace
{

rv_editor_progress feed(std::initializer_list<const char *> lines)
{
    rv_editor_progress progress;
    for (const char *const line : lines) {
        rv_editor_progress_feed(progress, line);
    }
    return progress;
}

const char *const plan5 = "mppcburner: [plan] sources manifest compile assets burn";

} // namespace

RV_EDITOR_TEST(rv_editor_test_steps_marks)
{
    RV_EDITOR_CHECK(rv_editor_step_mark(rv_editor_step_state::done) == 'v');
    RV_EDITOR_CHECK(rv_editor_step_mark(rv_editor_step_state::running) == '>');
    RV_EDITOR_CHECK(rv_editor_step_mark(rv_editor_step_state::pending) == ' ');
    RV_EDITOR_CHECK(rv_editor_step_mark(rv_editor_step_state::failed) == 'x');
    RV_EDITOR_CHECK(rv_editor_step_mark(rv_editor_step_state::interrupted) == '!');
}

RV_EDITOR_TEST(rv_editor_test_steps_compact_running)
{
    const auto p =
        feed({ plan5, "mppcburner: [1/5] sources a", "mppcburner: [2/5] manifest b", "mppcburner: [3/5] compile c" });
    RV_EDITOR_CHECK(rv_editor_steps_compact(p.runs.at(0)) == "step 3/5: compile");
}

RV_EDITOR_TEST(rv_editor_test_steps_compact_plan_only_names_no_step)
{
    const auto p = feed({ plan5 });
    RV_EDITOR_CHECK(rv_editor_steps_compact(p.runs.at(0)).empty());
}

RV_EDITOR_TEST(rv_editor_test_steps_compact_all_done)
{
    const auto p = feed({ "mppcburner: [plan] a b", "mppcburner: [1/2] a", "mppcburner: [2/2] b", "mppcburner: [end] ok" });
    RV_EDITOR_CHECK(rv_editor_steps_compact(p.runs.at(0)).empty());
}

RV_EDITOR_TEST(rv_editor_test_steps_compact_failed_names_failed_step)
{
    const auto p = feed(
        { plan5, "mppcburner: [1/5] sources a", "mppcburner: [2/5] manifest b", "mppcburner: [end] failed: manifest is bad" });
    const auto &run = p.runs.at(0);
    RV_EDITOR_CHECK(run.steps.at(1).state == rv_editor_step_state::failed); // the parser marks it, not this widget
    RV_EDITOR_CHECK(rv_editor_steps_compact(run) == "step 2/5: manifest");
}

RV_EDITOR_TEST(rv_editor_test_steps_compact_interrupted)
{
    auto p = feed({ plan5, "mppcburner: [1/5] sources a", "mppcburner: [2/5] manifest b" });
    rv_editor_progress_finish(p);
    RV_EDITOR_CHECK(rv_editor_steps_compact(p.runs.at(0)) == "step 2/5: manifest");
}

RV_EDITOR_TEST(rv_editor_test_steps_compact_no_plan_counts_from_n)
{
    const auto p = feed({ "mppcburner: [1/3] sources a" });
    RV_EDITOR_CHECK(rv_editor_steps_compact(p.runs.at(0)) == "step 1/3: sources");
}

// The compact text is a bounded template (R5): `step n/N: name`, n and N up to 99, the name up to 16 characters (the
// progress protocol rejects longer), so chrome can reserve its width.
RV_EDITOR_TEST(rv_editor_test_steps_compact_fits_the_template_at_its_limits)
{
    const std::string name(16, 'x');
    const auto p = feed({ ("mppcburner: [99/99] " + name + " detail").c_str() });
    RV_EDITOR_CHECK(rv_editor_steps_compact(p.runs.at(0)) == "step 99/99: " + name);
    RV_EDITOR_CHECK(rv_editor_steps_compact(p.runs.at(0)).size() <= std::string("step 99/99: ").size() + 16);
}

RV_EDITOR_TEST(rv_editor_test_steps_compact_empty)
{
    RV_EDITOR_CHECK(rv_editor_steps_compact(rv_editor_run{}).empty());
    RV_EDITOR_CHECK(rv_editor_steps_compact(rv_editor_progress{}).empty());
}

RV_EDITOR_TEST(rv_editor_test_steps_compact_job_takes_latest_run)
{
    const auto p = feed({ "mppcburner: [plan] a b",
        "mppcburner: [1/2] a",
        "mppcburner: [end] ok",
        "mppcburner: [plan] x y z",
        "mppcburner: [2/3] y" });
    RV_EDITOR_CHECK(p.runs.size() == 2);
    RV_EDITOR_CHECK(rv_editor_steps_compact(p) == "step 2/3: y");
}

RV_EDITOR_TEST(rv_editor_test_steps_layout_one_cell_per_step)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const auto p =
        feed({ plan5, "mppcburner: [1/5] sources a", "mppcburner: [2/5] manifest b", "mppcburner: [3/5] compile c" });
    gui.frame([&] {
        const auto l = rv_editor_steps_layout_make(p.runs.at(0), 2000.0f);
        RV_EDITOR_CHECK(l.cells.size() == 5);
        RV_EDITOR_CHECK(l.rows == 1);
        if (l.cells.size() == 5) {
            RV_EDITOR_CHECK(l.cells[0].text == "[v] sources");
            RV_EDITOR_CHECK(l.cells[2].text == "[>] compile");
            RV_EDITOR_CHECK(l.cells[3].text == "[ ] assets");
            RV_EDITOR_CHECK(l.cells[0].min.x < l.cells[1].min.x);
            RV_EDITOR_CHECK(l.cells[0].min.y == l.cells[4].min.y);
        }
    });
}

RV_EDITOR_TEST(rv_editor_test_steps_layout_wraps_by_width)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const auto p = feed({ plan5 });
    gui.frame([&] {
        const auto wide = rv_editor_steps_layout_make(p.runs.at(0), 2000.0f);
        const auto narrow = rv_editor_steps_layout_make(p.runs.at(0), 120.0f);
        RV_EDITOR_CHECK(wide.rows == 1);
        RV_EDITOR_CHECK(narrow.rows > 1);
        RV_EDITOR_CHECK(narrow.cells.size() == 5);
        if (narrow.cells.size() == 5) {
            RV_EDITOR_CHECK(narrow.cells.back().min.y > narrow.cells.front().min.y);
        }
        for (const auto &c : narrow.cells) {
            RV_EDITOR_CHECK(c.max.x <= 120.0f + 1.0f);
        }
    });
}

RV_EDITOR_TEST(rv_editor_test_steps_layout_bar_is_run_permille)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const auto p =
        feed({ plan5, "mppcburner: [1/5] sources a", "mppcburner: [2/5] manifest b", "mppcburner: [3/5] compile c" });
    gui.frame([&] {
        const auto l = rv_editor_steps_layout_make(p.runs.at(0), 800.0f);
        RV_EDITOR_CHECK(rv_editor_run_permille(p.runs.at(0)) > 0);
        RV_EDITOR_CHECK(l.permille == rv_editor_run_permille(p.runs.at(0)));
    });
}

RV_EDITOR_TEST(rv_editor_test_steps_draw_returns_layout_and_takes_room)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const auto p = feed({ plan5, "mppcburner: [1/5] sources a", "mppcburner: [2/5] manifest b" });
    const rv_editor_theme theme = rv_editor_theme_olive;
    gui.frame([&] {
        const float before = ImGui::GetCursorPosY();
        const auto l = rv_editor_steps_draw(p.runs.at(0), theme);
        RV_EDITOR_CHECK(l.cells.size() == 5);
        RV_EDITOR_CHECK(l.permille == rv_editor_run_permille(p.runs.at(0)));
        RV_EDITOR_CHECK(ImGui::GetCursorPosY() > before);
    });
}
