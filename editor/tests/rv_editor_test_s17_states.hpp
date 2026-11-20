// The states of the S17 tests' panes (the plan's table, one function per kind) as the fixture sets them up.

#pragma once

#include "app/rv_editor_app_findings.hpp"
#include "rv_editor_test_s17.hpp"

namespace rv_editor_test
{

namespace s17
{

// ---- Export ------------------------------------------------------------------------------------------------------

inline void fixture::export_states()
{
    const auto idle = [this](rv_editor_export_kind k) {
        return [this, k] {
            end_jobs();
            open_project();
            rv_editor_pane_export_select_kind(k);
        };
    };
    // A run of the burner stand-in that ends as `want` (cancelled by the test when `cancel`).
    const auto run = [this](const char *body, rv_editor_build_state want, bool cancel) {
        return [this, body, want, cancel] {
            end_jobs();
            open_project();
            pane_state::write_script(where / "burner.sh", std::string(body) + "\n");
            std::error_code ec;
            fs::remove_all(where / "export", ec); // what an earlier state left there
            rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
            RV_EDITOR_CHECK(rv_editor_pane_export_start(app()) == RV_OK);
            if (cancel) {
                std::string e;
                RV_EDITOR_CHECK(app().exporter.cancel(e) == RV_OK);
            }
            export_tile::settle(app(), want);
            RV_EDITOR_CHECK(app().exporter.state() == want);
        };
    };
    states = {
        { "no project",
            [this] {
                end_jobs();
                app().project.open = false;
            },
            false },
        { "disc", idle(rv_editor_export_kind::disc), true },
        { "player", idle(rv_editor_export_kind::player), true },
        { "web", idle(rv_editor_export_kind::web), true },
        { "web setup running",
            [this] {
                end_jobs();
                open_project();
                pane_state::write_script(where / "burner.sh",
                    "echo 'mppcburner: [plan] download unpack'; echo 'mppcburner: [1/2] download x 10%'; sleep 30\n");
                rv_editor_pane_export_select_kind(rv_editor_export_kind::web);
                std::string e;
                RV_EDITOR_CHECK(app().exporter.start_setup(app().tools, app().log, e) == RV_OK);
                for (int i = 0; i < 100 && app().exporter.progress().runs.empty(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    (void)app().exporter.update(app().log, e);
                }
            },
            true },
        { "running",
            run("echo 'mppcburner: [plan] sources compile assets'; echo 'mppcburner: [1/3] sources x'; sleep 30",
                rv_editor_build_state::building,
                false),
            true },
        { "succeeded", run("mkdir -p \"$6\" && echo x > \"$6/new.txt\"", rv_editor_build_state::succeeded, false), true },
        { "failed",
            run("echo 'mppcburner: [plan] sources compile'; echo boom >&2; exit 3", rv_editor_build_state::failed, false),
            true },
        { "cancelled", run("sleep 30", rv_editor_build_state::cancelled, true), true },
        { "refused",
            [this] {
                end_jobs();
                open_project();
                rv_editor_pane_export_select_kind(rv_editor_export_kind::disc);
                draw(400.0f, 400.0f); // the first frame offers the default directory
                std::error_code ec;
                fs::remove_all(where / "export", ec); // a directory an earlier export marked would be replaced, not refused
                fs::create_directories(where / "export" / "mppcdisc", ec);
                std::ofstream(where / "export" / "mppcdisc" / "not_ours.txt") << "x";
                RV_EDITOR_CHECK(rv_editor_pane_export_start(app()) != RV_OK);
            },
            true },
    };
}

// ---- Release Candidate and Checks --------------------------------------------------------------------------------

inline void fixture::candidate_states()
{
    const auto with = [this](std::function<void(rv_editor_release &)> change) {
        return [this, change] {
            open_project();
            app().release = {};
            change(app().release);
        };
    };
    const auto ones = [](size_t n) {
        return [n](rv_editor_release &r) {
            for (size_t i = 0; i < n; ++i) {
                r.candidates.push_back(candidate(static_cast<uint32_t>(i + 1)));
            }
            r.selected = n - 1;
        };
    };
    states = {
        { "no project",
            [this] {
                app().project.open = false;
            },
            false },
        { "no candidates", with([](rv_editor_release &) {}), true, 1 },
        { "one", with(ones(1)), true },
        { "several",
            with([ones](rv_editor_release &r) {
                ones(3)(r);
                r.selected = 1;
            }),
            true },
        { "bytes changed",
            with([ones](rv_editor_release &r) {
                ones(1)(r);
                r.candidates[0].bytes_changed = true;
            }),
            true },
        { "hashing",
            with([ones](rv_editor_release &r) {
                ones(1)(r);
                r.candidates[0].sha256.clear();
            }),
            true },
        { "approved",
            with([ones](rv_editor_release &r) {
                ones(1)(r);
                r.candidates[0].decision = rv_editor_decision::approved;
                r.candidates[0].decided_at = "2026-01-02 11:00";
                r.candidates[0].operator_name = "an operator with a rather long name";
            }),
            true },
        { "rejected",
            with([ones](rv_editor_release &r) {
                ones(1)(r);
                r.candidates[0].decision = rv_editor_decision::rejected;
                r.candidates[0].decided_at = "2026-01-02 11:00";
                r.candidates[0].operator_name = "operator";
            }),
            true },
        { "error",
            with([ones](rv_editor_release &r) {
                ones(2)(r);
                r.last_failure = s13::words(300);
                r.error = s13::words(300);
                r.report = "/" + solid(path_chars - 1, 'r');
            }),
            true },
    };
}

inline void fixture::checks_states()
{
    const auto with = [this](std::function<void(rv_editor_candidate &)> change) {
        return [this, change] {
            open_project();
            app().release = {};
            app().release.candidates.push_back(candidate(1));
            change(app().release.candidates[0]);
        };
    };
    states = {
        { "no project",
            [this] {
                app().project.open = false;
            },
            false },
        { "no candidate",
            [this] {
                open_project();
                app().release = {};
            },
            true },
        { "not run", with([](rv_editor_candidate &) {}), true },
        { "all states",
            with([](rv_editor_candidate &c) {
                rv_editor_check_set(c, 0, rv_editor_check_state::passed, "ok", "env");
                rv_editor_check_set(c, 3, rv_editor_check_state::failed, "bad", "env");
                rv_editor_check_set(c, 4, rv_editor_check_state::blocked, "no", "env");
                rv_editor_check_set(c, 5, rv_editor_check_state::skipped, "skip", "env");
                rv_editor_check_set(c, 6, rv_editor_check_state::running, "run", "env");
            }),
            true },
        { "void",
            with([](rv_editor_candidate &c) {
                rv_editor_check_set(c, 0, rv_editor_check_state::passed, "ok", "env");
                rv_editor_check_set(c, 3, rv_editor_check_state::passed, "ok", "env");
                c.checks[3].hash = "other";
                c.bytes_changed = true;
            }),
            true },
    };
}

// ---- Observe and Session -----------------------------------------------------------------------------------------

inline void fixture::observe_states()
{
    const auto keys = [this](const pane_state::fake_console_options &o, std::vector<std::string> pins) {
        return [this, o, pins] {
            live_session(o);
            RV_EDITOR_CHECK(pane_state::ask_fake_session(app(), "keys"));
            app().observe = {};
            app().observe.pins = pins;
            app().observe.read_frame = app().session.frame();
        };
    };
    pane_state::fake_console_options no_lua;
    no_lua.with_lua = false;
    pane_state::fake_console_options long_disc;
    long_disc.disc_hex = hex_of(solid(path_chars, 'd'));
    states = {
        { "no project",
            [this] {
                end_session();
                app().project.open = false;
            },
            false },
        { "no session",
            [this] {
                end_session();
                open_project();
            },
            true,
            -1 },
        { "ended",
            [this] {
                live_session();
                RV_EDITOR_CHECK(pane_state::end_fake_session(app()));
            },
            true,
            -1 },
        { "waiting",
            [this] {
                silent_session();
            },
            true,
            -1 },
        { "no Lua", keys(no_lua, {}), true, -1 },
        { "pins and keys", keys({}, { "p1", "p2" }), true, -1 },
        { "long values", keys(long_disc, { solid(path_chars, 'k') }), true, -1 },
    };
}

inline void fixture::session_states()
{
    pane_state::fake_console_options long_disc;
    long_disc.disc_hex = hex_of(solid(path_chars, 'd'));
    states = {
        { "no project",
            [this] {
                end_session();
                app().project.open = false;
            },
            false },
        { "no session",
            [this] {
                end_session();
                open_project();
            },
            true,
            -1 },
        { "live",
            [this] {
                live_session();
            },
            true,
            -1 },
        { "ended",
            [this] {
                live_session();
                RV_EDITOR_CHECK(pane_state::end_fake_session(app()));
            },
            true,
            -1 },
        { "crashed",
            [this] {
                live_session();
                ::kill(app().session.pid(), SIGKILL);
                RV_EDITOR_CHECK(pane_state::pump_session(app(), [&] {
                    return !app().session.live();
                }));
                RV_EDITOR_CHECK(app().session.state() == rv_editor_run_state::crashed);
            },
            true,
            -1 },
        { "long values",
            [this, long_disc] {
                live_session(long_disc);
                app().session_profile = solid(path_chars, 'f');
                RV_EDITOR_CHECK(rv_editor_app_add_mark(app(), s13::words(300)) == RV_OK);
            },
            true,
            -1 },
    };
}

// ---- Build Result ------------------------------------------------------------------------------------------------

inline void fixture::build_result_states()
{
    // A build job of the burner stand-in that does `body`; waits for the end when `wait`.
    const auto job = [this](const char *body, bool wait) {
        return [this, body, wait] {
            end_jobs();
            open_project();
            pane_state::write_script(where / "burner.sh", std::string(body) + "\n");
            std::string e;
            RV_EDITOR_CHECK(app().build.start(app().project, app().tools, app().log, e) == RV_OK);
            for (int i = 0; wait && i < 300 && app().build.busy(); ++i) {
                (void)app().build.update(app().log, e);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
    };
    states = {
        { "no project",
            [this] {
                end_jobs();
                app().project.open = false;
            },
            false },
        { "idle",
            [this] {
                open_project();
            },
            true },
        { "building", job("sleep 30", false), true },
        { "cancelled",
            [this] {
                end_jobs();
                open_project();
                pane_state::write_script(where / "burner.sh", "sleep 30\n");
                std::string e;
                RV_EDITOR_CHECK(app().build.start(app().project, app().tools, app().log, e) == RV_OK);
                RV_EDITOR_CHECK(app().build.cancel(e) == RV_OK);
                end_jobs();
            },
            true },
        { "failed with diagnostics",
            [this, failed = job("echo boom >&2\nexit 1", true)] {
                failed();
                for (int i = 0; i < 3; ++i) {
                    app().log.add(rv_editor_log_source::build,
                        rv_editor_log_level::error,
                        "error: line " + std::to_string(i) + " " + s13::words(i == 1 ? 300 : 20));
                }
            },
            true },
        { "succeeded", job("exit 0", true), true },
    };
}

inline void fixture::build_states()
{
    switch (kind) {
    case rv_editor_pane_kind::export_:
        export_states();
        break;
    case rv_editor_pane_kind::candidate:
        candidate_states();
        break;
    case rv_editor_pane_kind::checks:
        checks_states();
        break;
    case rv_editor_pane_kind::observe:
        observe_states();
        break;
    case rv_editor_pane_kind::session:
        session_states();
        break;
    case rv_editor_pane_kind::build_result:
        build_result_states();
        break;
    default:
        states = { { "the one state", [] {}, false } };
        break;
    }
}

} // namespace s17

} // namespace rv_editor_test
