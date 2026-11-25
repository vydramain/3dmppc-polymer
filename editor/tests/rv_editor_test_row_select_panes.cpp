// AC3 in the Candidate/Checks and Observe panes: a row of a table answers a single click anywhere on it (it becomes the
// selected row, which shows in the drawing), and a double click or Enter on it does the row's main action:
//   Checks row (manual, not set, passable): marks the check Passed, as its Pass button does; otherwise nothing.
//   Observe key row of a table: Open (descend into it); any other key row and every pin row: nothing beyond select.

#include <functional>
#include <string>

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

constexpr float pane_width = 600.0f;
const rv_editor_theme theme = rv_editor_theme_olive;

using body_fn = std::function<void()>;

void frames(tall_gui &g, const body_fn &body, int n = 1)
{
    for (int i = 0; i < n; ++i) {
        frame_hash(g, pane_width, body);
    }
}

uint64_t hash_now(tall_gui &g, const body_fn &body)
{
    return frame_hash(g, pane_width, body);
}

void move_to(tall_gui &g, const body_fn &body, ImVec2 p)
{
    ImGui::GetIO().AddMousePosEvent(p.x, p.y);
    frames(g, body);
}

void away(tall_gui &g, const body_fn &body)
{
    ImGui::GetIO().AddMousePosEvent(-1000.0f, -1000.0f);
    frames(g, body, 2);
}

// One frame that moves the test clock past ImGui's double click time, so the next click counts as a single click.
void let_double_click_expire(tall_gui &g, const body_fn &body)
{
    ImGuiIO &io = ImGui::GetIO();
    const float delta = io.DeltaTime;
    io.DeltaTime = 2.0f * io.MouseDoubleClickTime;
    frames(g, body);
    io.DeltaTime = delta;
}

// A press and a release over two frames at the point the pointer is at.
void click_here(tall_gui &g, const body_fn &body)
{
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frames(g, body);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frames(g, body);
}

void click_at(tall_gui &g, const body_fn &body, ImVec2 p, int times = 1)
{
    move_to(g, body, p);
    for (int i = 0; i < times; ++i) {
        click_here(g, body);
    }
    frames(g, body);
}

void press_enter(tall_gui &g, const body_fn &body)
{
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    frames(g, body);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    frames(g, body);
}

// The row of an item `button` sits in: y of its top and bottom edges; x across the pane (the left edge of the first
// column, the middle of the stretched columns).
struct row_area {
    float top, bottom, left, mid;
};

row_area area_of(const rv_editor_probe_record &r, const rv_editor_probe_entry &button)
{
    float min_x = button.x0;
    for (const auto &e : r.entries) {
        min_x = e.x0 < min_x ? e.x0 : min_x;
    }
    return { button.y0, button.y1, min_x + 12.0f, (min_x + button.x0) / 2.0f };
}

// Clicks the four corners (3 px in, left and mid in x) and the centre of row `a`; each time row `b` is selected
// before, so a pane that never marks a selection (the drawing stays as it was) does not pass. Returns how many of the
// five clicks changed the drawing against the drawing with `b` selected.
int clicks_that_select(tall_gui &g, const body_fn &body, const row_area &a, const row_area &b)
{
    const ImVec2 pts[5] = { { a.left, a.top + 3.0f },
        { a.mid, a.top + 3.0f },
        { a.left, a.bottom - 3.0f },
        { a.mid, a.bottom - 3.0f },
        { a.mid, (a.top + a.bottom) / 2.0f } };
    int hits = 0;
    for (const ImVec2 &p : pts) {
        let_double_click_expire(g, body);
        click_at(g, body, { b.mid, (b.top + b.bottom) / 2.0f });
        away(g, body);
        const uint64_t with_b = hash_now(g, body);
        let_double_click_expire(g, body);
        click_at(g, body, p);
        away(g, body);
        hits += hash_now(g, body) != with_b ? 1 : 0;
    }
    return hits;
}

// ---- Checks / Candidate ------------------------------------------------------------------------------------------

constexpr size_t first_manual = 3; // Launch
constexpr size_t second_manual = 4;

// An open project with one hashed candidate that was played once; its manual checks are not set.
void make_candidate(rv_editor_app &app, const fs::path &where)
{
    open_project(app, where);
    rv_editor_candidate c;
    c.number = 1;
    c.sha256 = "abcdef0123456789abcdef0123456789";
    c.size = 10;
    c.built_at = "2026-01-01 10:00";
    c.checks = rv_editor_checks_make();
    c.playtests = 1;
    c.last_run_end = "quit";
    c.last_run_clean = true;
    app.release.candidates.push_back(std::move(c));
}

rv_editor_check_state state_of(const rv_editor_app &app, size_t id)
{
    return app.release.candidates[0].checks[id].state;
}

struct candidate_fixture {
    tall_gui g;
    fs::path where = fresh_dir("rv_editor_row_select_candidate");
    rv_editor_app app;
    body_fn body;
    rv_editor_probe_record rec;
    row_area first{}, second{};
    explicit candidate_fixture(bool checks_pane)
    {
        make_candidate(app, where);
        body = [this, checks_pane] {
            if (checks_pane) {
                rv_editor_pane_checks(app, theme);
            } else {
                rv_editor_pane_candidate(app, theme);
            }
        };
        rec = settled(g, pane_width, body);
        const auto *a = nth_item(rec, rv_editor_text("pane_candidate.pass_button"), 0);
        const auto *b = nth_item(rec, rv_editor_text("pane_candidate.pass_button"), 1);
        RV_EDITOR_CHECK(a != nullptr && b != nullptr);
        if (a != nullptr && b != nullptr) {
            first = area_of(rec, *a);
            second = area_of(rec, *b);
        }
    }
    ~candidate_fixture()
    {
        remove_dir(where);
    }
};

void single_click_selects_check_row(bool checks_pane)
{
    candidate_fixture f(checks_pane);
    RV_EDITOR_CHECK(clicks_that_select(f.g, f.body, f.first, f.second) == 5);
    // A single click only selects: no check was set.
    RV_EDITOR_CHECK(state_of(f.app, first_manual) == rv_editor_check_state::not_run);
    RV_EDITOR_CHECK(state_of(f.app, second_manual) == rv_editor_check_state::not_run);
}

void double_click_passes_check_row(bool checks_pane)
{
    candidate_fixture f(checks_pane);
    // On a row's first column, away from its buttons.
    click_at(f.g, f.body, { f.first.left, (f.first.top + f.first.bottom) / 2.0f }, 2);
    RV_EDITOR_CHECK(state_of(f.app, first_manual) == rv_editor_check_state::passed);
    RV_EDITOR_CHECK(state_of(f.app, second_manual) == rv_editor_check_state::not_run);
    // A check already set stays as it is.
    click_at(f.g, f.body, { f.first.left, (f.first.top + f.first.bottom) / 2.0f }, 2);
    RV_EDITOR_CHECK(state_of(f.app, first_manual) == rv_editor_check_state::passed);
}

void enter_passes_check_row(bool checks_pane)
{
    candidate_fixture f(checks_pane);
    click_at(f.g, f.body, { f.second.mid, (f.second.top + f.second.bottom) / 2.0f });
    RV_EDITOR_CHECK(state_of(f.app, second_manual) == rv_editor_check_state::not_run);
    press_enter(f.g, f.body);
    RV_EDITOR_CHECK(state_of(f.app, second_manual) == rv_editor_check_state::passed);
    RV_EDITOR_CHECK(state_of(f.app, first_manual) == rv_editor_check_state::not_run);
}

// What the Pass button refuses, a double click refuses too: the bytes are not the ones built (no hash).
void double_click_respects_why_not_pass(bool checks_pane)
{
    candidate_fixture f(checks_pane);
    f.app.release.candidates[0].playtests = 0; // the candidate was never run
    click_at(f.g, f.body, { f.first.left, (f.first.top + f.first.bottom) / 2.0f }, 2);
    RV_EDITOR_CHECK(state_of(f.app, first_manual) == rv_editor_check_state::not_run);
    // ...yet the click selected the row.
    const uint64_t selected = hash_now(f.g, f.body);
    click_at(f.g, f.body, { f.second.mid, (f.second.top + f.second.bottom) / 2.0f });
    away(f.g, f.body);
    RV_EDITOR_CHECK(hash_now(f.g, f.body) != selected);
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_row_select_checks_single_click_anywhere)
{
    single_click_selects_check_row(true);
}

RV_EDITOR_TEST(rv_editor_test_row_select_candidate_single_click_anywhere)
{
    single_click_selects_check_row(false);
}

RV_EDITOR_TEST(rv_editor_test_row_select_checks_double_click_passes_manual_check)
{
    double_click_passes_check_row(true);
}

RV_EDITOR_TEST(rv_editor_test_row_select_candidate_double_click_passes_manual_check)
{
    double_click_passes_check_row(false);
}

RV_EDITOR_TEST(rv_editor_test_row_select_checks_enter_passes_manual_check)
{
    enter_passes_check_row(true);
}

RV_EDITOR_TEST(rv_editor_test_row_select_candidate_enter_passes_manual_check)
{
    enter_passes_check_row(false);
}

RV_EDITOR_TEST(rv_editor_test_row_select_checks_double_click_respects_why_not_pass)
{
    double_click_respects_why_not_pass(true);
}

// ---- Observe -----------------------------------------------------------------------------------------------------

namespace
{

struct observe_fixture {
    tall_gui g;
    fs::path where = fresh_dir("rv_editor_row_select_observe");
    rv_editor_app app;
    body_fn body;
    rv_editor_probe_record rec;
    bool ready = false;
    observe_fixture(const std::vector<std::string> &pins = {})
    {
        open_project(app, where);
        ready = start_fake_session(app, where, {}) && ask_fake_session(app, "keys");
        RV_EDITOR_CHECK(ready);
        app.observe.pins = pins;
        app.observe.read_frame = app.session.frame();
        body = [this] {
            rv_editor_pane_observe(app, theme);
        };
        rec = settled(g, pane_width, body);
    }
    ~observe_fixture()
    {
        std::string e;
        app.session.shutdown(app.log, e);
        remove_dir(where);
    }
};

row_area area(const observe_fixture &f, const char *button, int n)
{
    const auto *e = nth_item(f.rec, rv_editor_text(button), n);
    RV_EDITOR_CHECK(e != nullptr);
    return e != nullptr ? area_of(f.rec, *e) : row_area{};
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_row_select_observe_key_row_single_click_anywhere)
{
    observe_fixture f;
    if (!f.ready) {
        return;
    }
    const row_area alpha = area(f, "pane_observe.open", 0); // a table: Open
    const row_area beta = area(f, "pane_observe.pin", 0);   // a number: Pin
    RV_EDITOR_CHECK(clicks_that_select(f.g, f.body, alpha, beta) == 5);
    RV_EDITOR_CHECK(clicks_that_select(f.g, f.body, beta, alpha) == 5);
    // Selecting is all a single click does.
    RV_EDITOR_CHECK(f.app.requests.empty());
}

RV_EDITOR_TEST(rv_editor_test_row_select_observe_pin_row_single_click_anywhere)
{
    observe_fixture f({ "p1", "p2" });
    if (!f.ready) {
        return;
    }
    const row_area p1 = area(f, "pane_observe.unpin", 0);
    const row_area p2 = area(f, "pane_observe.unpin", 1);
    RV_EDITOR_CHECK(clicks_that_select(f.g, f.body, p1, p2) == 5);
    RV_EDITOR_CHECK(f.app.requests.empty());
}

RV_EDITOR_TEST(rv_editor_test_row_select_observe_double_click_opens_a_table_key)
{
    observe_fixture f;
    if (!f.ready) {
        return;
    }
    const row_area alpha = area(f, "pane_observe.open", 0);
    click_at(f.g, f.body, { alpha.left, (alpha.top + alpha.bottom) / 2.0f }, 2);
    RV_EDITOR_CHECK(f.app.requests.size() == 1);
    if (f.app.requests.size() == 1) {
        RV_EDITOR_CHECK(f.app.requests[0].kind == rv_editor_pane_request_kind::observe_open);
        RV_EDITOR_CHECK(f.app.requests[0].name == "alpha");
    }
}

RV_EDITOR_TEST(rv_editor_test_row_select_observe_enter_opens_a_table_key)
{
    observe_fixture f;
    if (!f.ready) {
        return;
    }
    const row_area alpha = area(f, "pane_observe.open", 0);
    click_at(f.g, f.body, { alpha.mid, (alpha.top + alpha.bottom) / 2.0f });
    RV_EDITOR_CHECK(f.app.requests.empty());
    press_enter(f.g, f.body);
    RV_EDITOR_CHECK(f.app.requests.size() == 1);
    if (f.app.requests.size() == 1) {
        RV_EDITOR_CHECK(f.app.requests[0].kind == rv_editor_pane_request_kind::observe_open);
        RV_EDITOR_CHECK(f.app.requests[0].name == "alpha");
    }
}

// A key that is not a table has no main action beyond select, and a pin row none either (the buttons stay).
RV_EDITOR_TEST(rv_editor_test_row_select_observe_double_click_elsewhere_only_selects)
{
    observe_fixture f({ "p1" });
    if (!f.ready) {
        return;
    }
    const row_area beta = area(f, "pane_observe.pin", 0);
    const row_area pin = area(f, "pane_observe.unpin", 0);
    away(f.g, f.body);
    const uint64_t before = hash_now(f.g, f.body);
    click_at(f.g, f.body, { beta.left, (beta.top + beta.bottom) / 2.0f }, 2);
    click_at(f.g, f.body, { pin.left, (pin.top + pin.bottom) / 2.0f }, 2);
    away(f.g, f.body);
    RV_EDITOR_CHECK(f.app.requests.empty());
    // ...and the double clicks did select (the drawing has a selected row now).
    RV_EDITOR_CHECK(hash_now(f.g, f.body) != before);
}
