// A build that does not start or fails leaves a title and a reason for Problems and asks for Problems to come forward:
// the reason picked from the job's log, and the app's transitions (refused, ended, cleared).

#include <string>
#include <vector>

#include "app/rv_editor_app.hpp"
#include "build/rv_editor_build_alert.hpp"
#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;

namespace
{

constexpr auto src_build = rv_editor_log_source::build;
constexpr auto src_candidate = rv_editor_log_source::candidate;
constexpr auto src_editor = rv_editor_log_source::editor;
constexpr auto lv_info = rv_editor_log_level::info;
constexpr auto lv_warn = rv_editor_log_level::warning;
constexpr auto lv_error = rv_editor_log_level::error;

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

// Starts a job in the app's log the way rv_editor_app_build_saved does.
void job_start(rv_editor_app &app)
{
    app.build_first_seq = app.log.revision() + 1;
}

size_t count_lines(const rv_editor_app &app, rv_editor_log_level level, const std::string &text)
{
    size_t n = 0;
    for (const rv_editor_log_line &l : app.log.lines()) {
        if (l.level == level && l.text == text) {
            ++n;
        }
    }
    return n;
}

// A project that is open and could build, so only the cause under test refuses.
void open_project(rv_editor_app &app)
{
    app.project.open = true;
}

} // namespace

// --- reason ---------------------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_build_alert_reason_is_last_error_line_of_this_job)
{
    rv_editor_log log;
    log.add(src_build, lv_error, "error: old job line");
    const uint64_t first = log.revision() + 1;
    log.add(src_build, lv_info, "mppcburner: [1/4] sources");
    log.add(src_build, lv_error, "error: first of this job");
    log.add(src_build, lv_error, "error: scene.toml:3: unknown key");
    log.add(src_build, lv_info, "ninja: build stopped");
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(log, first, "exit code 1") == "error: scene.toml:3: unknown key");
}

RV_EDITOR_TEST(rv_editor_test_build_alert_reason_ignores_errors_of_the_previous_job)
{
    rv_editor_log log;
    log.add(src_build, lv_error, "error: old job line");
    const uint64_t first = log.revision() + 1;
    log.add(src_build, lv_info, "mppcburner: [1/4] sources");
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(log, first, "exit code 3") == "exit code 3");
}

RV_EDITOR_TEST(rv_editor_test_build_alert_reason_falls_back_to_exit_text)
{
    rv_editor_log log;
    log.add(src_build, lv_info, "mppcburner: [1/4] sources");
    log.add(src_build, lv_warn, "warning: something odd");
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(log, 1, "exit code 2") == "exit code 2");
    rv_editor_log empty;
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(empty, 1, "killed by KILL") == "killed by KILL");
}

RV_EDITOR_TEST(rv_editor_test_build_alert_reason_takes_only_burner_lines)
{
    rv_editor_log log;
    log.add(src_build, lv_error, "error: burner says");
    log.add(src_editor, lv_error, "cannot build: something of the editor");
    log.add(rv_editor_log_source::runtime, lv_error, "error: the runtime says");
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(log, 1, "exit code 1") == "error: burner says");
}

RV_EDITOR_TEST(rv_editor_test_build_alert_reason_counts_candidate_lines)
{
    rv_editor_log log;
    log.add(src_candidate, lv_error, "error: candidate refused: 2 inspector findings");
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(log, 1, "exit code 1") == "error: candidate refused: 2 inspector findings");
}

// --- the editor did not start the build -----------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_build_alert_reason_prefers_the_failed_run_reason_of_the_progress_tree)
{
    rv_editor_progress progress;
    rv_editor_progress_feed(progress, "mppcburner: [plan] sources manifest");
    rv_editor_progress_feed(progress, "mppcburner: [1/2] sources x");
    rv_editor_progress_feed(progress, "mppcburner: [end] failed: sources have error findings");
    rv_editor_log log;
    const uint64_t first = log.revision() + 1;
    log.add(src_build, lv_error, "error: a later line");
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(log, first, "exit code 1", progress) == "sources have error findings");
}

RV_EDITOR_TEST(rv_editor_test_build_alert_reason_without_a_failed_run_is_the_last_error_line)
{
    rv_editor_log log;
    const uint64_t first = log.revision() + 1;
    log.add(src_build, lv_error, "error: the last one");
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(log, first, "exit code 1", rv_editor_progress()) == "error: the last one");

    rv_editor_progress interrupted;
    rv_editor_progress_feed(interrupted, "mppcburner: [plan] sources manifest");
    rv_editor_progress_feed(interrupted, "mppcburner: [1/2] sources x");
    rv_editor_progress_finish(interrupted);
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(log, first, "exit code 1", interrupted) == "error: the last one");
    rv_editor_log empty;
    RV_EDITOR_CHECK(rv_editor_build_alert_reason(empty, 1, "exit code 9", interrupted) == "exit code 9");
}

RV_EDITOR_TEST(rv_editor_test_build_alert_refused_by_why_not_build)
{
    load_texts();
    rv_editor_app app; // no project open
    const std::string why = rv_editor_app_why_not_build(app);
    RV_EDITOR_CHECK(!why.empty());
    RV_EDITOR_CHECK(rv_editor_app_build(app) == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    RV_EDITOR_CHECK(app.build_alert.reason == why);
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
    // R7: the log line is what it was.
    RV_EDITOR_CHECK(count_lines(app, lv_warn, "not built: " + why) == 1);
}

RV_EDITOR_TEST(rv_editor_test_build_alert_refused_by_burner_problem_in_build_saved)
{
    load_texts();
    rv_editor_app app;
    open_project(app);
    app.tools.burner.problem = "mppcburner was not found in the tools directory";
    RV_EDITOR_CHECK(rv_editor_app_build_saved(app) == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    RV_EDITOR_CHECK(app.build_alert.reason == app.tools.burner.problem);
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
}

RV_EDITOR_TEST(rv_editor_test_build_alert_start_error_is_the_reason)
{
    load_texts();
    rv_editor_app app;
    open_project(app); // no cache dir: build.start refuses with its own error
    const std::string why = rv_editor_text("build.no_cache_dir");
    RV_EDITOR_CHECK(rv_editor_app_build_saved(app) != RV_OK);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    RV_EDITOR_CHECK(app.build_alert.reason == why);
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
    RV_EDITOR_CHECK(count_lines(app, lv_error, "cannot build: " + why) == 1);
}

RV_EDITOR_TEST(rv_editor_test_build_alert_run_that_must_build_is_refused_with_the_build_reason)
{
    load_texts();
    rv_editor_app app;
    open_project(app);
    app.project.state_dir = "/tmp/rv-state"; // never touched: Run is refused before it
    app.tools.burner.problem = "mppcburner is not executable";
    RV_EDITOR_CHECK(rv_editor_app_run_saved(app) == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    RV_EDITOR_CHECK(app.build_alert.reason == app.tools.burner.problem);
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
    RV_EDITOR_CHECK(count_lines(app, lv_warn, "not run: " + app.tools.burner.problem) == 1);
}

RV_EDITOR_TEST(rv_editor_test_build_alert_run_refused_for_a_run_reason_is_not_a_build_failure)
{
    load_texts();
    rv_editor_app app;
    open_project(app);
    app.project.state_dir = "/tmp/rv-state";
    app.run_problem = "the profile has no arguments";
    RV_EDITOR_CHECK(rv_editor_app_run_saved(app) == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::none);
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::empty);
    // The control: a build reason on the same app does set it.
    app.run_problem.clear();
    app.tools.burner.problem = "mppcburner is not executable";
    RV_EDITOR_CHECK(rv_editor_app_run_saved(app) == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
}

// R1a, AC8: the Run hotkey goes through rv_editor_app_run, which refuses before rv_editor_app_run_saved.
RV_EDITOR_TEST(rv_editor_test_build_alert_run_entry_that_must_build_is_refused_with_the_build_reason)
{
    load_texts();
    rv_editor_app app;
    open_project(app);
    app.project.state_dir = "/tmp/rv-state"; // never touched: Run is refused before it
    app.tools.burner.problem = "mppcburner is not executable";
    RV_EDITOR_CHECK(rv_editor_app_run(app) == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    RV_EDITOR_CHECK(app.build_alert.reason == app.tools.burner.problem);
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
    RV_EDITOR_CHECK(count_lines(app, lv_warn, "not run: " + app.tools.burner.problem) == 1);
}

RV_EDITOR_TEST(rv_editor_test_build_alert_run_entry_refused_for_a_run_reason_sets_no_alert)
{
    load_texts();
    rv_editor_app app;
    open_project(app);
    app.project.state_dir = "/tmp/rv-state";
    app.run_problem = "the profile has no arguments";
    RV_EDITOR_CHECK(rv_editor_app_run(app) == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::none);
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::empty);
    RV_EDITOR_CHECK(count_lines(app, lv_warn, "not run: " + app.run_problem) == 1);
}

RV_EDITOR_TEST(rv_editor_test_build_alert_build_and_restart_refused)
{
    load_texts();
    rv_editor_app app;
    open_project(app);
    app.tools.baker.problem = "no baker directory";
    RV_EDITOR_CHECK(rv_editor_app_build_restart_saved(app) == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    RV_EDITOR_CHECK(app.build_alert.reason == "no baker directory");
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
}

RV_EDITOR_TEST(rv_editor_test_build_alert_candidate_start_refused)
{
    load_texts();
    rv_editor_app app;
    open_project(app);
    app.tools.burner.problem = "mppcburner is gone";
    RV_EDITOR_CHECK(rv_editor_app_build_candidate(app) == RV_ERR_BUSY);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    RV_EDITOR_CHECK(app.build_alert.reason == "mppcburner is gone");
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
    RV_EDITOR_CHECK(!app.release.building);
}

RV_EDITOR_TEST(rv_editor_test_build_alert_refused_sets_text_directly)
{
    rv_editor_app app;
    rv_editor_app_build_alert_refused(app, "because");
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    RV_EDITOR_CHECK(app.build_alert.reason == "because");
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
}

// --- a build job ends -----------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_build_alert_failed_job_sets_alert_from_its_log)
{
    rv_editor_app app;
    app.log.add(src_build, lv_error, "error: previous build line");
    job_start(app);
    app.log.add(src_build, lv_error, "error: scene.toml:3: unknown key");
    app.log.add(src_build, lv_error, "error: the sources check found 1 error");
    rv_editor_app_build_alert_ended(app, rv_editor_build_state::failed, "exit code 1");
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::failed);
    RV_EDITOR_CHECK(app.build_alert.reason == "error: the sources check found 1 error");
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
}

RV_EDITOR_TEST(rv_editor_test_build_alert_failed_job_without_errors_says_exit_text)
{
    rv_editor_app app;
    app.log.add(src_build, lv_error, "error: previous build line");
    job_start(app);
    app.log.add(src_build, lv_info, "mppcburner: [1/4] sources");
    rv_editor_app_build_alert_ended(app, rv_editor_build_state::failed, "exit code 139");
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::failed);
    RV_EDITOR_CHECK(app.build_alert.reason == "exit code 139");
}

RV_EDITOR_TEST(rv_editor_test_build_alert_cancelled_and_succeeded_jobs_leave_nothing)
{
    {
        // The control: the same job ending failed does set it.
        rv_editor_app app;
        job_start(app);
        app.log.add(src_build, lv_error, "error: even if a line says so");
        rv_editor_app_build_alert_ended(app, rv_editor_build_state::failed, "exit code 1");
        RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::failed);
    }
    for (const rv_editor_build_state ended : { rv_editor_build_state::cancelled, rv_editor_build_state::succeeded }) {
        rv_editor_app app;
        job_start(app);
        app.log.add(src_build, lv_error, "error: even if a line says so");
        rv_editor_app_build_alert_ended(app, ended, "exit code 0");
        RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::none);
        RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::empty);
    }
}

RV_EDITOR_TEST(rv_editor_test_build_alert_second_failure_replaces_the_first)
{
    rv_editor_app app;
    job_start(app);
    app.log.add(src_build, lv_error, "error: first failure");
    rv_editor_app_build_alert_ended(app, rv_editor_build_state::failed, "exit code 1");
    RV_EDITOR_CHECK(app.build_alert.reason == "error: first failure");
    job_start(app);
    app.log.add(src_build, lv_error, "error: second failure");
    rv_editor_app_build_alert_ended(app, rv_editor_build_state::failed, "exit code 1");
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::failed);
    RV_EDITOR_CHECK(app.build_alert.reason == "error: second failure");
}

RV_EDITOR_TEST(rv_editor_test_build_alert_refusal_after_failure_replaces_text_and_kind)
{
    load_texts();
    rv_editor_app app;
    job_start(app);
    app.log.add(src_build, lv_error, "error: it failed");
    rv_editor_app_build_alert_ended(app, rv_editor_build_state::failed, "exit code 1");
    rv_editor_app_build_alert_refused(app, "the burner is gone");
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    RV_EDITOR_CHECK(app.build_alert.reason == "the burner is gone");
}

RV_EDITOR_TEST(rv_editor_test_build_alert_clear_removes_the_alert)
{
    rv_editor_app app;
    rv_editor_app_build_alert_refused(app, "because");
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::not_started);
    rv_editor_app_build_alert_clear(app);
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::none);
    RV_EDITOR_CHECK(app.build_alert.reason.empty());
}

// --- Run that waited for the build ----------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_build_alert_run_waiting_for_failed_build_adds_one_alert_and_one_line)
{
    load_texts();
    rv_editor_app app;
    app.run_after_build = true;
    job_start(app);
    app.log.add(src_build, lv_error, "error: compile failed");
    rv_editor_app_build_alert_ended(app, rv_editor_build_state::failed, "exit code 1");
    (void)rv_editor_app_run_after_build_refuse(app);
    // The Run's own refusal does not replace the burner's reason nor ask twice.
    RV_EDITOR_CHECK(app.build_alert.kind == rv_editor_build_alert_kind::failed);
    RV_EDITOR_CHECK(app.build_alert.reason == "error: compile failed");
    RV_EDITOR_CHECK(app.show_request == rv_editor_pane_kind::inspector_report);
    size_t not_run = 0;
    for (const rv_editor_log_line &l : app.log.lines()) {
        not_run += l.source == src_editor && l.text.rfind("not run:", 0) == 0 ? 1 : 0;
    }
    RV_EDITOR_CHECK(not_run == 1);
}
