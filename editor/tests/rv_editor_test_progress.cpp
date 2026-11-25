// The run tree built from a job's output lines: plan, steps, sub-runs, ends, percent.

#include <string>

#include "build/rv_editor_progress.hpp"
#include "rv_editor_test.hpp"

using namespace rv_editor;

namespace
{

constexpr rv_editor_step_state pending = rv_editor_step_state::pending;
constexpr rv_editor_step_state running = rv_editor_step_state::running;
constexpr rv_editor_step_state done = rv_editor_step_state::done;
constexpr rv_editor_step_state failed = rv_editor_step_state::failed;
constexpr rv_editor_step_state interrupted = rv_editor_step_state::interrupted;

void feed_all(rv_editor_progress &progress, const char *const *lines)
{
    for (; *lines != nullptr; ++lines) {
        rv_editor_progress_feed(progress, *lines);
    }
}

bool states_are(const rv_editor_run &run, const std::vector<rv_editor_step_state> &want)
{
    if (run.steps.size() != want.size()) {
        return false;
    }
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (run.steps[i].state != want[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_progress_plan_then_steps_in_order)
{
    rv_editor_progress progress;
    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [plan] sources manifest compile"));
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1) {
        return;
    }
    const rv_editor_run &run = progress.runs[0];
    RV_EDITOR_CHECK(run.tool == "mppcburner");
    RV_EDITOR_CHECK(run.end == rv_editor_run_end::open);
    RV_EDITOR_CHECK(states_are(run, { pending, pending, pending }));
    if (run.steps.size() != 3) {
        return;
    }
    RV_EDITOR_CHECK(run.steps[0].label == "sources");
    RV_EDITOR_CHECK(run.steps[1].label == "manifest");
    RV_EDITOR_CHECK(run.steps[2].label == "compile");

    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [1/3] sources ./squid-maid"));
    RV_EDITOR_CHECK(states_are(run, { running, pending, pending }));
    RV_EDITOR_CHECK(run.steps[0].detail == "./squid-maid");

    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [2/3] manifest disc.toml"));
    RV_EDITOR_CHECK(states_are(run, { done, running, pending }));

    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [3/3] compile"));
    RV_EDITOR_CHECK(states_are(run, { done, done, running }));

    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [end] ok"));
    RV_EDITOR_CHECK(run.end == rv_editor_run_end::ok);
    RV_EDITOR_CHECK(states_are(run, { done, done, done }));
}

RV_EDITOR_TEST(rv_editor_test_progress_nested_sub_run_failed)
{
    const char *const lines[] = {
        "mppcburner: [plan] sources manifest compile assets burn",
        "mppcburner: [1/5] sources ./squid-maid",
        "mppcinspector: [plan] disc scenes",
        "mppcinspector: [1/2] disc disc.toml",
        "mppcinspector: [2/2] scenes 3 file(s)",
        "mppcinspector: [end] failed: 2 error(s)",
        "mppcburner: [end] failed: at sources",
        nullptr,
    };
    rv_editor_progress progress;
    feed_all(progress, lines);
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1) {
        return;
    }
    const rv_editor_run &burner = progress.runs[0];
    RV_EDITOR_CHECK(burner.tool == "mppcburner");
    RV_EDITOR_CHECK(burner.end == rv_editor_run_end::failed);
    RV_EDITOR_CHECK(burner.reason == "at sources");
    RV_EDITOR_CHECK(states_are(burner, { failed, pending, pending, pending, pending }));
    if (burner.steps.empty()) {
        return;
    }
    const rv_editor_step &sources = burner.steps[0];
    RV_EDITOR_CHECK(sources.runs.size() == 1);
    if (sources.runs.size() != 1) {
        return;
    }
    const rv_editor_run &inspector = sources.runs[0];
    RV_EDITOR_CHECK(inspector.tool == "mppcinspector");
    RV_EDITOR_CHECK(inspector.end == rv_editor_run_end::failed);
    RV_EDITOR_CHECK(inspector.reason == "2 error(s)");
    RV_EDITOR_CHECK(states_are(inspector, { done, failed }));
    // Only the step that was running when the sub-plan came carries sub-runs.
    for (std::size_t i = 1; i < burner.steps.size(); ++i) {
        RV_EDITOR_CHECK(burner.steps[i].runs.empty());
    }
}

RV_EDITOR_TEST(rv_editor_test_progress_sub_run_ok_then_parent_continues)
{
    const char *const lines[] = {
        "mppcburner: [plan] sources manifest",
        "mppcburner: [1/2] sources .",
        "mppcinspector: [plan] disc",
        "mppcinspector: [1/1] disc disc.toml",
        "mppcinspector: [end] ok",
        "mppcburner: [2/2] manifest disc.toml",
        nullptr,
    };
    rv_editor_progress progress;
    feed_all(progress, lines);
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1 || progress.runs[0].steps.size() != 2) {
        return;
    }
    const rv_editor_run &burner = progress.runs[0];
    RV_EDITOR_CHECK(states_are(burner, { done, running }));
    RV_EDITOR_CHECK(burner.steps[0].runs.size() == 1);
    RV_EDITOR_CHECK(burner.steps[1].runs.empty());
    if (burner.steps[0].runs.size() == 1) {
        RV_EDITOR_CHECK(burner.steps[0].runs[0].end == rv_editor_run_end::ok);
    }
}

RV_EDITOR_TEST(rv_editor_test_progress_unknown_step_name_is_a_normal_cell)
{
    rv_editor_progress progress;
    rv_editor_progress_feed(progress, "mppcburner: [plan] sources frobnicate burn");
    rv_editor_progress_feed(progress, "mppcburner: [1/3] sources .");
    rv_editor_progress_feed(progress, "mppcburner: [2/3] frobnicate new thing");
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1) {
        return;
    }
    const rv_editor_run &run = progress.runs[0];
    RV_EDITOR_CHECK(states_are(run, { done, running, pending }));
    if (run.steps.size() == 3) {
        RV_EDITOR_CHECK(run.steps[1].label == "frobnicate");
        RV_EDITOR_CHECK(run.steps[1].detail == "new thing");
    }
}

RV_EDITOR_TEST(rv_editor_test_progress_no_plan_cells_from_n)
{
    rv_editor_progress progress;
    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [2/5] manifest disc.toml"));
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1) {
        return;
    }
    const rv_editor_run &run = progress.runs[0];
    RV_EDITOR_CHECK(run.tool == "mppcburner");
    RV_EDITOR_CHECK(states_are(run, { done, running, pending, pending, pending }));
    if (run.steps.size() == 5) {
        RV_EDITOR_CHECK(run.steps[1].label == "manifest");
    }
    rv_editor_progress_feed(progress, "mppcburner: [3/5] compile");
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    RV_EDITOR_CHECK(states_are(progress.runs[0], { done, done, running, pending, pending }));
}

RV_EDITOR_TEST(rv_editor_test_progress_no_end_at_job_exit_is_interrupted)
{
    rv_editor_progress progress;
    rv_editor_progress_feed(progress, "mppcburner: [plan] sources manifest compile");
    rv_editor_progress_feed(progress, "mppcburner: [1/3] sources .");
    rv_editor_progress_feed(progress, "mppcburner: [2/3] manifest disc.toml");
    rv_editor_progress_finish(progress);
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1) {
        return;
    }
    RV_EDITOR_CHECK(progress.runs[0].end == rv_editor_run_end::interrupted);
    RV_EDITOR_CHECK(states_are(progress.runs[0], { done, interrupted, pending }));
}

RV_EDITOR_TEST(rv_editor_test_progress_finish_interrupts_open_sub_run_too)
{
    rv_editor_progress progress;
    rv_editor_progress_feed(progress, "mppcburner: [plan] sources manifest");
    rv_editor_progress_feed(progress, "mppcburner: [1/2] sources .");
    rv_editor_progress_feed(progress, "mppcinspector: [plan] disc scenes");
    rv_editor_progress_feed(progress, "mppcinspector: [1/2] disc disc.toml");
    rv_editor_progress_finish(progress);
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1 || progress.runs[0].steps.empty() || progress.runs[0].steps[0].runs.size() != 1) {
        return;
    }
    RV_EDITOR_CHECK(states_are(progress.runs[0], { interrupted, pending }));
    const rv_editor_run &inspector = progress.runs[0].steps[0].runs[0];
    RV_EDITOR_CHECK(inspector.end == rv_editor_run_end::interrupted);
    RV_EDITOR_CHECK(states_are(inspector, { interrupted, pending }));
}

RV_EDITOR_TEST(rv_editor_test_progress_finish_keeps_a_finished_run)
{
    rv_editor_progress progress;
    rv_editor_progress_feed(progress, "mppcburner: [plan] sources");
    rv_editor_progress_feed(progress, "mppcburner: [1/1] sources .");
    rv_editor_progress_feed(progress, "mppcburner: [end] ok");
    rv_editor_progress_finish(progress);
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1) {
        return;
    }
    RV_EDITOR_CHECK(progress.runs[0].end == rv_editor_run_end::ok);
    RV_EDITOR_CHECK(states_are(progress.runs[0], { done }));
}

RV_EDITOR_TEST(rv_editor_test_progress_foreign_lines_ignored)
{
    rv_editor_progress progress;
    RV_EDITOR_CHECK(!rv_editor_progress_feed(progress, "-- Configuring done"));
    RV_EDITOR_CHECK(progress.runs.empty());
    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [plan] sources compile"));
    RV_EDITOR_CHECK(!rv_editor_progress_feed(progress, "[12/340] Building CXX object foo.o"));
    RV_EDITOR_CHECK(!rv_editor_progress_feed(progress, "disc.toml:3: error: bad key"));
    RV_EDITOR_CHECK(!rv_editor_progress_feed(progress, ""));
    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [1/2] sources ."));
    RV_EDITOR_CHECK(!rv_editor_progress_feed(progress, "ninja: build stopped"));
    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [2/2] compile"));
    RV_EDITOR_CHECK(rv_editor_progress_feed(progress, "mppcburner: [end] ok"));
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() == 1) {
        RV_EDITOR_CHECK(progress.runs[0].end == rv_editor_run_end::ok);
        RV_EDITOR_CHECK(states_are(progress.runs[0], { done, done }));
    }
}

RV_EDITOR_TEST(rv_editor_test_progress_refusal_before_first_step)
{
    rv_editor_progress progress;
    rv_editor_progress_feed(progress, "mppcburner: [plan] sources manifest compile");
    rv_editor_progress_feed(progress, "mppcburner: [end] failed: no disc.toml in ./x");
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1) {
        return;
    }
    RV_EDITOR_CHECK(progress.runs[0].end == rv_editor_run_end::failed);
    RV_EDITOR_CHECK(progress.runs[0].reason == "no disc.toml in ./x");
    RV_EDITOR_CHECK(states_are(progress.runs[0], { pending, pending, pending }));
}

RV_EDITOR_TEST(rv_editor_test_progress_feed_text_splits_on_cr_and_lf)
{
    rv_editor_progress progress;
    RV_EDITOR_CHECK(rv_editor_progress_feed_text(progress,
        "mppcburner: [plan] a b c\r\nsome log text\n"
        "mppcburner: [1/3] a x 10%\rmppcburner: [1/3] a x 60%\rmppcburner: [2/3] b y\r\n"));
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() == 1) {
        RV_EDITOR_CHECK(states_are(progress.runs[0], { done, running, pending }));
    }
    RV_EDITOR_CHECK(!rv_editor_progress_feed_text(progress, "just log text\r\nmore\n"));
}

RV_EDITOR_TEST(rv_editor_test_progress_setup_percent_from_detail)
{
    rv_editor_progress progress;
    rv_editor_progress_feed(progress, "mppcburner: [plan] a download c d e f g");
    rv_editor_progress_feed(progress, "mppcburner: [2/7] download emsdk.tar.xz 38%");
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1 || progress.runs[0].steps.size() != 7) {
        RV_EDITOR_CHECK(false);
        return;
    }
    const rv_editor_run &run = progress.runs[0];
    RV_EDITOR_CHECK(run.steps[1].state == running);
    RV_EDITOR_CHECK(rv_editor_step_percent(run.steps[1]) == 38);
    RV_EDITOR_CHECK(rv_editor_run_permille(run) == (1000 + 380) / 7);

    // A redrawn line of the same step replaces the detail.
    rv_editor_progress_feed(progress, "mppcburner: [2/7] download emsdk.tar.xz 90%");
    RV_EDITOR_CHECK(rv_editor_step_percent(progress.runs[0].steps[1]) == 90);
    RV_EDITOR_CHECK(progress.runs[0].steps[1].state == running);
}

RV_EDITOR_TEST(rv_editor_test_progress_step_percent_shapes)
{
    rv_editor_step step;
    step.detail = "emsdk.tar.xz: 55% of 120 MiB";
    RV_EDITOR_CHECK(rv_editor_step_percent(step) == 55);
    step.detail = "emsdk.tar.xz 100%";
    RV_EDITOR_CHECK(rv_editor_step_percent(step) == 100);
    step.detail = "emsdk.tar.xz size unknown";
    RV_EDITOR_CHECK(rv_editor_step_percent(step) == -1);
    step.detail = "emsdk.tar.xz 150%";
    RV_EDITOR_CHECK(rv_editor_step_percent(step) == -1);
    step.detail = "";
    RV_EDITOR_CHECK(rv_editor_step_percent(step) == -1);
}

RV_EDITOR_TEST(rv_editor_test_progress_permille_without_percent_and_when_ended)
{
    rv_editor_progress progress;
    rv_editor_progress_feed(progress, "mppcburner: [plan] a b c d");
    RV_EDITOR_CHECK(progress.runs.size() == 1);
    if (progress.runs.size() != 1) {
        return;
    }
    RV_EDITOR_CHECK(rv_editor_run_permille(progress.runs[0]) == 0);
    rv_editor_progress_feed(progress, "mppcburner: [3/4] c x");
    RV_EDITOR_CHECK(rv_editor_run_permille(progress.runs[0]) == 2 * 1000 / 4);
    rv_editor_progress_feed(progress, "mppcburner: [end] ok");
    RV_EDITOR_CHECK(rv_editor_run_permille(progress.runs[0]) == 1000);
    RV_EDITOR_CHECK(rv_editor_run_permille(rv_editor_run()) == 0);
}
