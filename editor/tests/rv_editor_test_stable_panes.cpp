// AC9 and AC6 in the Candidate, Observe, Session and Build Result panes: within a mode nothing appears, moves or
// resizes by state (a line, a row, a button, a column width, an area), and text wraps or reads in full instead of
// running past the pane. Switching the whole mode (no candidate to a table, session live to ended) is allowed.

#include <string>
#include <vector>

#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_pane_state.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::pane_state;

namespace
{

constexpr float width = 400.0f;
const rv_editor_theme theme = rv_editor_theme_olive;

std::string long_text()
{
    std::string s;
    while (s.size() < 300) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

// ---- Candidate ---------------------------------------------------------------------------------------------------

rv_editor_candidate make_candidate(uint32_t number)
{
    rv_editor_candidate c;
    c.number = number;
    c.sha256 = "abcdef0123456789abcdef0123456789";
    c.size = 10;
    c.built_at = "2026-01-01 10:00";
    c.checks = rv_editor_checks_make();
    c.image = "/tmp/rv_editor_stable_candidate_image.iso";
    return c;
}

// Two candidates, the newest selected: the dropdown is there in every state.
struct candidate_fixture {
    tall_gui g;
    rv_editor_app app;
    fs::path where = fresh_dir("rv_editor_stable_candidate");
    std::function<void()> body;
    candidate_fixture()
    {
        open_project(app, where);
        app.release.candidates.push_back(make_candidate(1));
        app.release.candidates.push_back(make_candidate(2));
        app.release.selected = 1;
        body = [this] {
            rv_editor_pane_candidate(app, theme);
        };
    }
    ~candidate_fixture()
    {
        remove_dir(where);
    }
    rv_editor_probe_record draw(float w = width)
    {
        return settled(g, w, body);
    }
};

// The pane drawn in its base state and in the state `change` makes; "" when nothing appeared, moved or resized.
template <class F>
std::string candidate_difference(F change)
{
    candidate_fixture f;
    const auto base = f.draw();
    change(f.app);
    return layout_difference(base, f.draw());
}

void report_difference(const std::string &d)
{
    if (!d.empty()) {
        std::fprintf(stderr, "  %s\n", d.c_str());
    }
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_stable_candidate_failure_line_keeps_its_place)
{
    const std::string d = candidate_difference([](rv_editor_app &app) {
        app.release.last_failure = long_text();
    });
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_candidate_newer_candidate_line_keeps_its_place)
{
    const std::string d = candidate_difference([](rv_editor_app &app) {
        app.release.selected = 0; // a newer one exists
    });
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_candidate_bytes_changed_line_keeps_its_place)
{
    const std::string d = candidate_difference([](rv_editor_app &app) {
        app.release.candidates[1].bytes_changed = true;
    });
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_candidate_report_path_row_keeps_its_place)
{
    const std::string d = candidate_difference([](rv_editor_app &app) {
        app.release.report = "/home/someone/projects/a/rather/long/path/to/the/release/report/file/report-0001.txt";
    });
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_candidate_all_lines_together_keep_their_places)
{
    const std::string d = candidate_difference([](rv_editor_app &app) {
        app.release.last_failure = long_text();
        app.release.selected = 0;
        app.release.candidates[0].bytes_changed = true;
        app.release.report = "/tmp/report-0001.txt";
        app.release.error = long_text();
    });
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

// The state column keeps its width in every state, so the Pass button of the first manual check stays where it is.
RV_EDITOR_TEST(rv_editor_test_stable_candidate_state_column_keeps_its_width)
{
    candidate_fixture f;
    f.app.release.candidates[1].playtests = 1;
    const char *pass = rv_editor_text("pane_candidate.pass_button");
    const auto not_run = f.draw();
    const std::string hash = f.app.release.candidates[1].sha256;
    rv_editor_check_set(f.app.release.candidates[1], 3, rv_editor_check_state::passed, "seen", "env");
    const auto passed = f.draw();
    // Another hash: the result is void, which its text says.
    f.app.release.candidates[1].checks[3].hash = "other";
    f.app.release.candidates[1].checks[3].state = rv_editor_check_state::skipped;
    const auto voided = f.draw();
    const std::string d1 = item_difference(not_run, passed, pass);
    const std::string d2 = item_difference(not_run, voided, pass);
    report_difference(d1);
    report_difference(d2);
    RV_EDITOR_CHECK(d1.empty());
    RV_EDITOR_CHECK(d2.empty());
}

// The Checks header names the candidate and its summary; in a narrow tile it wraps instead of running past the edge.
RV_EDITOR_TEST(rv_editor_test_stable_checks_header_wraps_in_a_narrow_tile)
{
    candidate_fixture f;
    f.body = [&f] {
        rv_editor_pane_checks(f.app, theme);
    };
    const float narrow = 200.0f;
    const auto rec = f.draw(narrow);
    const std::string d = text_overflow(rec, narrow);
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

// ---- Observe -----------------------------------------------------------------------------------------------------

namespace
{

struct observe_fixture {
    tall_gui g;
    rv_editor_app app;
    fs::path where = fresh_dir("rv_editor_stable_observe");
    std::function<void()> body;
    bool ready = false;
    observe_fixture()
    {
        open_project(app, where);
        ready = start_fake_session(app, where, {}) && ask_fake_session(app, "keys") && ask_fake_session(app, "keys alpha");
        RV_EDITOR_CHECK(ready);
        app.observe.read_frame = app.session.frame();
        body = [this] {
            rv_editor_pane_observe(app, theme);
        };
    }
    ~observe_fixture()
    {
        std::string e;
        app.session.shutdown(app.log, e);
        remove_dir(where);
    }
    rv_editor_probe_record draw()
    {
        return settled(g, 600.0f, body);
    }
};

} // namespace

// Up is always there, disabled at the root: the path section keeps its place on the root and in a table.
RV_EDITOR_TEST(rv_editor_test_stable_observe_up_button_present_at_root_and_nested)
{
    observe_fixture f;
    if (!f.ready) {
        return;
    }
    const auto root = f.draw();
    f.app.observe.path = { "alpha" };
    const auto nested = f.draw();
    const char *up = rv_editor_text("pane_observe.up");
    const std::string d = item_difference(root, nested, up);
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

// The Open and the Pin button share one column: the same left edge and width.
RV_EDITOR_TEST(rv_editor_test_stable_observe_open_and_pin_buttons_share_a_column)
{
    observe_fixture f;
    if (!f.ready) {
        return;
    }
    const auto rec = f.draw();
    const auto *open = find_item(rec, rv_editor_text("pane_observe.open"));
    const auto *pin = find_item(rec, rv_editor_text("pane_observe.pin"));
    RV_EDITOR_CHECK(open != nullptr && pin != nullptr);
    if (open != nullptr && pin != nullptr) {
        RV_EDITOR_CHECK(open->x0 == pin->x0);
        RV_EDITOR_CHECK(open->x1 == pin->x1);
    }
}

// ---- Session -----------------------------------------------------------------------------------------------------

namespace
{

// A session with every optional row (disc, entry script, note for an older build, last reload) or with none.
struct session_fixture {
    tall_gui &g;
    rv_editor_app app;
    fs::path where;
    bool ready = false;
    session_fixture(tall_gui &gui, const char *name, bool rich)
        : g(gui)
        , where(fresh_dir(name))
    {
        open_project(app, where);
        fake_console_options o;
        o.with_disc = rich;
        o.with_lua = rich;
        o.build_number = rich ? 5 : app.build.number();
        ready = start_fake_session(app, where, o);
        if (rich && ready) {
            std::string e;
            ready = app.session.reload(app.log, e) == RV_OK && pump_session(app, [&] {
                return !app.session.reload_result().empty();
            });
        }
        RV_EDITOR_CHECK(ready);
    }
    ~session_fixture()
    {
        std::string e;
        app.session.shutdown(app.log, e);
        remove_dir(where);
    }
    rv_editor_probe_record draw(float w = width)
    {
        return settled(g, w, [this] {
            rv_editor_pane_session(app, theme);
        });
    }
};

} // namespace

RV_EDITOR_TEST(rv_editor_test_stable_session_live_optional_rows_keep_their_places)
{
    tall_gui g;
    session_fixture rich(g, "rv_editor_stable_session_rich", true);
    session_fixture bare(g, "rv_editor_stable_session_bare", false);
    if (!rich.ready || !bare.ready) {
        return;
    }
    const std::string d = layout_difference(rich.draw(), bare.draw());
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_session_ended_optional_rows_keep_their_places)
{
    tall_gui g;
    session_fixture rich(g, "rv_editor_stable_session_rich_ended", true);
    session_fixture bare(g, "rv_editor_stable_session_bare_ended", false);
    if (!rich.ready || !bare.ready) {
        return;
    }
    RV_EDITOR_CHECK(end_fake_session(rich.app));
    RV_EDITOR_CHECK(end_fake_session(bare.app));
    const std::string d = layout_difference(rich.draw(), bare.draw());
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

// A long mark wraps in a narrow tile instead of running past its edge.
RV_EDITOR_TEST(rv_editor_test_stable_session_mark_wraps_in_a_narrow_tile)
{
    tall_gui g;
    session_fixture s(g, "rv_editor_stable_session_mark", false);
    if (!s.ready) {
        return;
    }
    s.app.marks.push_back("a mark with a rather long description of the moment it names, which goes on and on");
    const float narrow = 220.0f;
    const auto rec = s.draw(narrow);
    const std::string d = text_overflow(rec, narrow);
    report_difference(d);
    RV_EDITOR_CHECK(d.empty());
}

// ---- Build Result ------------------------------------------------------------------------------------------------

namespace
{

struct build_fixture {
    tall_gui g;
    rv_editor_app app;
    fs::path where = fresh_dir("rv_editor_stable_build_result");
    build_fixture()
    {
        open_project(app, where);
        std::string guard_error;
        rv_editor_guard_start(guard_error);
        app.tools.burner.path = where / "burner.sh";
        app.tools.baker.path = where / "burner.sh";
    }
    ~build_fixture()
    {
        std::string e;
        app.build.cancel(e);
        for (int i = 0; i < 300 && app.build.busy(); ++i) {
            app.build.update(app.log, e);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        remove_dir(where);
    }
    // Runs a burner that does `script` and waits for the job to end.
    rv_editor_build_state run(const std::string &script, bool wait)
    {
        write_script(where / "burner.sh", script);
        std::string e;
        RV_EDITOR_CHECK(app.build.start(app.project, app.tools, app.log, e) == RV_OK);
        for (int i = 0; wait && i < 300 && app.build.busy(); ++i) {
            app.build.update(app.log, e);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return app.build.state();
    }
    rv_editor_probe_record draw(float w = width)
    {
        return settled(g, w, [this] {
            rv_editor_pane_build_result(app, theme);
        });
    }
};

const char *const build_buttons[] = { "pane_build_result.build_button",
    "pane_build_result.build_candidate_button",
    "pane_build_result.cancel_button" };

} // namespace

// The three buttons keep their rectangles in every state of the job, and what the pane shows under them (a summary
// table, Open Log, a diagnostics area with or without lines) never moves or appears when the job's state changes.
RV_EDITOR_TEST(rv_editor_test_stable_build_result_buttons_and_diagnostics_keep_their_places)
{
    build_fixture f;
    const auto idle = f.draw();
    RV_EDITOR_CHECK(f.run("sleep 30\n", false) == rv_editor_build_state::building);
    const auto busy = f.draw();
    std::string e;
    f.app.build.cancel(e);
    for (int i = 0; i < 300 && f.app.build.busy(); ++i) {
        f.app.build.update(f.app.log, e);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    RV_EDITOR_CHECK(f.run("exit 0\n", true) == rv_editor_build_state::succeeded);
    const auto succeeded = f.draw();
    RV_EDITOR_CHECK(f.run("echo boom >&2\nexit 1\n", true) == rv_editor_build_state::failed);
    for (int i = 0; i < 3; ++i) {
        f.app.log.add(rv_editor_log_source::build, rv_editor_log_level::error, "error: line " + std::to_string(i));
    }
    const auto failed = f.draw();
    for (const char *key : build_buttons) {
        for (const auto *state : { &busy, &succeeded, &failed }) {
            const std::string d = item_difference(idle, *state, rv_editor_text(key));
            report_difference(d);
            RV_EDITOR_CHECK(d.empty());
        }
    }
    const std::string d1 = layout_difference(busy, succeeded);
    const std::string d2 = layout_difference(succeeded, failed);
    report_difference(d1);
    report_difference(d2);
    RV_EDITOR_CHECK(d1.empty());
    RV_EDITOR_CHECK(d2.empty());
}

// AC6: in a 15 em tile the buttons wrap onto further rows, each one fully inside the tile.
RV_EDITOR_TEST(rv_editor_test_stable_build_result_buttons_wrap_in_a_15em_tile)
{
    build_fixture f;
    float em = 0.0f;
    f.g.gui.frame([&] {
        em = ImGui::GetFontSize();
    });
    const float tile = 15.0f * em;
    const auto rec = f.draw(tile);
    int rows = 0;
    float last_y = -1.0f;
    for (const char *key : build_buttons) {
        const auto *b = find_item(rec, rv_editor_text(key));
        RV_EDITOR_CHECK(b != nullptr);
        if (b == nullptr) {
            continue;
        }
        if (b->x1 > tile) {
            std::fprintf(stderr,
                "  '%s' %s reaches past the tile width %.1f\n",
                key,
                rv_editor_probe_detail::rect(*b).c_str(),
                tile);
        }
        RV_EDITOR_CHECK(b->x0 >= 0.0f);
        RV_EDITOR_CHECK(b->x1 <= tile);
        rows += b->y0 > last_y + 1.0f ? 1 : 0;
        last_y = b->y0 > last_y ? b->y0 : last_y;
    }
    // They do not all fit in one row of this width.
    RV_EDITOR_CHECK(rows >= 2);
}
