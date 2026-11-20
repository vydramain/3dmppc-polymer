// S9, R11, AC12: under the buttons of Runtime Controls and Release Controls there is always a status, two text lines high,
// in a place that is the same in every state (no project included), whose text follows the state and which holds its
// longest text, wrapped at words, in its two lines at the minimum width.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "panes/rv_editor_pane_controls.hpp"
#include "panes/rv_editor_release_facts.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_s14.hpp"
#include "ui/rv_editor_widgets_strip_status.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s14;

namespace
{

constexpr float labels_from = 640.0f;
const std::string step16 = "[v][>][ ] " + std::string(16, 'x');

bool has(const std::string &s, const std::string &part)
{
    return s.find(part) != std::string::npos;
}

struct line {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool found = false;
};

line status_of(const rv_editor_probe_record &r)
{
    const auto *e = child_named(r, "##status");
    return e == nullptr ? line{} : line{ e->x0, e->y0, e->x1, e->y1, true };
}

// A shell's pane drawn in a tile `width` wide; returns the record and leaves the status text drawn in `text`.
rv_editor_probe_record
draw_status(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind, float width, std::string &text)
{
    const auto r = draw_at(gui, shell, kind, width, 400.0f);
    text = rv_editor_strip_status_last();
    return r;
}

// The height `text` takes wrapped at words in `width` px, and the two lines the status has.
float wrapped_height(rv_editor_test_imgui &gui, const std::string &text, float width)
{
    float h = 0.0f;
    gui.frame([&] {
        h = ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y;
    });
    return h;
}

float two_lines(rv_editor_test_imgui &gui)
{
    float h = 0.0f;
    gui.frame([&] {
        h = 2.0f * ImGui::GetTextLineHeight();
    });
    return h;
}

// The longest status Runtime Controls can show: every field at its limit.
const std::string worst_runtime = "Disconnected, session 999999 | Can't reload this disc: Restart (loses state)";

// The status is in every state, no project included, at one place and two text lines high, below the action slot; the
// strip is as high in every state.
void check_one_place(rv_editor_pane_kind kind)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(kind);
    const auto min = stable_tiles::pane_minimum(gui, kind);
    RV_EDITOR_CHECK(min.w > 0.0f);
    const float two = two_lines(gui);
    for (const float width : { std::ceil(min.w), labels_from, 1280.0f }) {
        line first;
        float first_height = -1.0f;
        for (size_t i = 0; i < all.shells.size(); ++i) {
            const auto r = draw_at(gui, *all.shells[i], kind, width, 400.0f);
            const line now = status_of(r);
            if (!now.found) {
                std::fprintf(stderr,
                    "  %s width %.0f, state '%s': no status line\n",
                    name_of(kind),
                    width,
                    all.names[i].c_str());
            }
            RV_EDITOR_CHECK(now.found);
            if (!now.found) {
                continue;
            }
            RV_EDITOR_CHECK(std::fabs((now.y1 - now.y0) - two) <= 0.5f);
            const auto *slot = child_named(r, "##action");
            RV_EDITOR_CHECK(slot != nullptr && now.y0 >= slot->y1 - 0.5f);
            if (first_height < 0.0f) {
                first = now;
                first_height = r.content_height;
            }
            const bool same = now.x0 == first.x0 && now.y0 == first.y0 && now.x1 == first.x1 && now.y1 == first.y1;
            if (!same || r.content_height != first_height) {
                std::fprintf(stderr,
                    "  %s width %.0f, state '%s': status line (%.1f,%.1f)-(%.1f,%.1f) strip %.1f high; first state "
                    "(%.1f,%.1f)-(%.1f,%.1f) strip %.1f\n",
                    name_of(kind),
                    width,
                    all.names[i].c_str(),
                    now.x0,
                    now.y0,
                    now.x1,
                    now.y1,
                    r.content_height,
                    first.x0,
                    first.y0,
                    first.x1,
                    first.y1,
                    first_height);
            }
            RV_EDITOR_CHECK(same);
            RV_EDITOR_CHECK(r.content_height == first_height);
        }
    }
}

} // namespace

// The widget: two text lines, the same rectangle for any text, the text whole (never "...").
RV_EDITOR_TEST(rv_editor_test_s9_strip_status_widget_is_two_constant_lines)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const float two = two_lines(gui);
    float height = -1.0f;
    gui.frame([&] {
        height = rv_editor_strip_status_height();
    });
    RV_EDITOR_CHECK(two > 0.0f && std::fabs(height - two) <= 0.5f);
    for (const float width : { 300.0f, 380.0f }) {
        line empty;
        for (const std::string &text : { std::string(), std::string("Running, session 1"), worst_runtime }) {
            const auto r = rv_editor_probe_draw(
                gui,
                400.0f,
                [&](rv_editor_probe_record &) {
                    rv_editor_strip_status(text, width, stable_tiles::theme);
                },
                300.0f);
            const line now = status_of(r);
            RV_EDITOR_CHECK(now.found);
            if (!now.found) {
                continue;
            }
            RV_EDITOR_CHECK(std::fabs((now.y1 - now.y0) - two) <= 0.5f);
            RV_EDITOR_CHECK(std::fabs((now.x1 - now.x0) - width) <= 0.5f);
            RV_EDITOR_CHECK(rv_editor_strip_status_last() == text);
            RV_EDITOR_CHECK(!has(rv_editor_strip_status_last(), "..."));
            if (!empty.found) {
                empty = now;
            }
            RV_EDITOR_CHECK(now.x0 == empty.x0 && now.y0 == empty.y0 && now.x1 == empty.x1 && now.y1 == empty.y1);
        }
    }
}

// Width 0: the room left of the row.
RV_EDITOR_TEST(rv_editor_test_s9_strip_status_widget_width_zero_takes_the_room_left)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    float room = 0.0f;
    const auto r = rv_editor_probe_draw(
        gui,
        400.0f,
        [&](rv_editor_probe_record &) {
            room = ImGui::GetContentRegionAvail().x;
            rv_editor_strip_status("Stopped", 0.0f, stable_tiles::theme);
        },
        300.0f);
    const line now = status_of(r);
    RV_EDITOR_CHECK(now.found && std::fabs((now.x1 - now.x0) - room) <= 0.5f);
}

// Runtime Controls' text: the session, then the message's brief, else the build's step.
RV_EDITOR_TEST(rv_editor_test_s9_controls_status_text_follows_the_session)
{
    load_texts();
    rv_editor_controls_status_input in;
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "Stopped");
    in.run_state = rv_editor_run_state::running;
    in.session_number = 1;
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "Running, session 1");
    in.run_state = rv_editor_run_state::paused;
    in.session_number = 12;
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "Paused, session 12");
    for (int i = 0; i <= static_cast<int>(rv_editor_run_state::refused); ++i) {
        in.run_state = static_cast<rv_editor_run_state>(i);
        RV_EDITOR_CHECK(rv_editor_controls_status(in).rfind(rv_editor_run_state_name(in.run_state), 0) == 0);
        RV_EDITOR_CHECK(has(rv_editor_controls_status(in), ", session 12"));
    }
}

RV_EDITOR_TEST(rv_editor_test_s9_controls_status_text_without_a_project)
{
    load_texts();
    rv_editor_controls_status_input in;
    in.project_open = false;
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "No project is open");
    in.run_state = rv_editor_run_state::running;
    in.session_number = 3;
    in.build_number = 7;
    in.build_step = "[v] compile";
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "No project is open");
}

RV_EDITOR_TEST(rv_editor_test_s9_controls_status_text_for_a_disc_that_cannot_reload)
{
    load_texts();
    rv_editor_controls_status_input in;
    in.run_state = rv_editor_run_state::running;
    in.session_number = 1;
    in.message.offer_build_restart = true;
    in.message.disc_cannot_reload = true;
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "Running, session 1 | Can't reload this disc: Restart (loses state)");
}

RV_EDITOR_TEST(rv_editor_test_s9_controls_status_text_for_a_build)
{
    load_texts();
    rv_editor_controls_status_input in;
    in.build_number = 7;
    in.build_step = "[v][>][ ] compile";
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "Stopped | Build #7 [v][>][ ] compile");
    in.run_state = rv_editor_run_state::running;
    in.session_number = 2;
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "Running, session 2 | Build #7 [v][>][ ] compile");
    // No build shown: no number or no step.
    in.build_step.clear();
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "Running, session 2");
    in.build_step = "[v] compile";
    in.build_number = 0;
    RV_EDITOR_CHECK(rv_editor_controls_status(in) == "Running, session 2");
    // The message wins over the build's step, as it did on the message line.
    in.build_number = 7;
    in.message.offer_build_restart = true;
    in.message.disc_cannot_reload = true;
    const std::string both = rv_editor_controls_status(in);
    RV_EDITOR_CHECK(both == "Running, session 2 | Can't reload this disc: Restart (loses state)");
    RV_EDITOR_CHECK(both.find('\n') == std::string::npos);
}

// The strip's place and height at every width, in every state with a project.
RV_EDITOR_TEST(rv_editor_test_s9_runtime_controls_status_line_is_one_place_in_every_state)
{
    check_one_place(rv_editor_pane_kind::controls);
}

RV_EDITOR_TEST(rv_editor_test_s9_release_controls_status_line_is_one_place_in_every_state)
{
    check_one_place(rv_editor_pane_kind::release_controls);
}

// What each state of Runtime Controls says.
RV_EDITOR_TEST(rv_editor_test_s9_runtime_controls_says_the_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(rv_editor_pane_kind::controls);
    const auto at = [&](const char *name) -> rv_editor_shell & {
        for (size_t i = 0; i < all.names.size(); ++i) {
            if (all.names[i] == name) {
                return *all.shells[i];
            }
        }
        RV_EDITOR_CHECK(false);
        return *all.shells[0];
    };
    std::string text;
    draw_status(gui, at("no project"), rv_editor_pane_kind::controls, 1000.0f, text);
    RV_EDITOR_CHECK(text == "No project is open");
    draw_status(gui, at("idle"), rv_editor_pane_kind::controls, 1000.0f, text);
    RV_EDITOR_CHECK(text == "Stopped");
    for (const char *name : { "running", "paused", "reload badge" }) {
        rv_editor_shell &shell = at(name);
        draw_status(gui, shell, rv_editor_pane_kind::controls, 1000.0f, text);
        const std::string head = std::string(rv_editor_run_state_name(shell.app.session.state())) + ", session " +
            std::to_string(shell.app.session.number());
        if (text.rfind(head, 0) != 0) {
            std::fprintf(stderr, "  state '%s': status '%s', want it to start with '%s'\n", name, text.c_str(), head.c_str());
        }
        RV_EDITOR_CHECK(text.rfind(head, 0) == 0);
    }
    rv_editor_shell &building = at("building");
    draw_status(gui, building, rv_editor_pane_kind::controls, 1000.0f, text);
    RV_EDITOR_CHECK(text.rfind("Stopped | Build #" + std::to_string(building.app.build.number()) + " ", 0) == 0);
    // The step's label comes from the plan (a 16 character name, step 2 of 3).
    RV_EDITOR_CHECK(has(text, "xxxxxxxxxxxxxxxx"));
    rv_editor_shell &failed = at("failed");
    draw_status(gui, failed, rv_editor_pane_kind::controls, 1000.0f, text);
    // No step to show after a failed build: the build's state follows the number, as in the status bar.
    RV_EDITOR_CHECK(text == "Stopped | Build #" + std::to_string(failed.app.build.number()) + " failed");
    RV_EDITOR_CHECK(text.find('\n') == std::string::npos);
}

// What each state of Release Controls says.
RV_EDITOR_TEST(rv_editor_test_s9_release_controls_says_the_candidate)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(rv_editor_pane_kind::release_controls);
    const auto at = [&](const char *name) -> rv_editor_shell & {
        for (size_t i = 0; i < all.names.size(); ++i) {
            if (all.names[i] == name) {
                return *all.shells[i];
            }
        }
        RV_EDITOR_CHECK(false);
        return *all.shells[0];
    };
    const auto kind = rv_editor_pane_kind::release_controls;
    std::string text;
    draw_status(gui, at("no project"), kind, 1000.0f, text);
    RV_EDITOR_CHECK(text == "No project is open");
    draw_status(gui, at("no candidate"), kind, 1000.0f, text);
    RV_EDITOR_CHECK(text == "No candidate yet");
    draw_status(gui, at("candidate"), kind, 1000.0f, text);
    const rv_editor_candidate big = candidate(999999, false);
    RV_EDITOR_CHECK(text == rv_editor_release_facts_make(&big, rv_editor_release_building::none).first_line);
    RV_EDITOR_CHECK(text.rfind("Candidate #999999 | Not ready | 0/8 checks | Not passed: ", 0) == 0);
    draw_status(gui, at("all passed"), kind, 1000.0f, text);
    RV_EDITOR_CHECK(text == "Candidate #3 | Ready | 8/8 checks");
    draw_status(gui, at("building"), kind, 1000.0f, text);
    RV_EDITOR_CHECK(text.rfind("Building candidate | Candidate #999999 | Not ready", 0) == 0);
    draw_status(gui, at("hung"), kind, 1000.0f, text);
    RV_EDITOR_CHECK(text.rfind("Candidate #3 | Not ready | 0/8 checks | Not passed: ", 0) == 0);
}

// AC12: with every field at its limit (a six digit number, a 16 character step, the longest message) every text fits the
// status in its two lines at the minimum width of the strip, wrapped at words.
RV_EDITOR_TEST(rv_editor_test_s9_runtime_controls_longest_status_fits_the_minimum_width)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(rv_editor_pane_kind::controls);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::controls);
    RV_EDITOR_CHECK(min.w > 0.0f);
    const float two = two_lines(gui);
    std::string shown;
    const auto r = draw_status(gui, *all.shells[0], rv_editor_pane_kind::controls, std::ceil(min.w), shown);
    const line now = status_of(r);
    RV_EDITOR_CHECK(now.found);
    const float width = now.x1 - now.x0;
    rv_editor_controls_message_input none, hung, gone, unanswered, detail, restart, build_restart;
    hung.force_stop = hung.hung = true;
    gone.force_stop = true;
    unanswered.uncertain = true;
    detail.detail = std::string(300, 'x');
    detail.outcome = rv_editor_controls_outcome::reload_refused;
    restart.offer_build_restart = restart.disc_cannot_reload = true;
    build_restart.offer_build_restart = true;
    build_restart.why_not_reload = "The change adds a new asset";
    for (const auto *m : { &hung, &gone, &unanswered, &detail, &restart, &build_restart, &none }) {
        for (int s = 0; s <= static_cast<int>(rv_editor_run_state::refused); ++s) {
            rv_editor_controls_status_input in;
            in.run_state = static_cast<rv_editor_run_state>(s);
            in.session_number = 999999;
            in.build_number = 999999;
            in.build_step = step16;
            in.message = *m;
            const std::string text = rv_editor_controls_status(in);
            const float h = wrapped_height(gui, text, width);
            if (!now.found || h > two + 0.5f) {
                std::fprintf(stderr,
                    "  status %.1f px wide at the minimum width %.0f: '%s' needs %.1f px high, the status has %.1f\n",
                    width,
                    std::ceil(min.w),
                    text.c_str(),
                    h,
                    two);
            }
            RV_EDITOR_CHECK(now.found && h <= two + 0.5f);
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_s9_release_controls_longest_status_fits_the_minimum_width)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(rv_editor_pane_kind::release_controls);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::release_controls);
    RV_EDITOR_CHECK(min.w > 0.0f);
    const float two = two_lines(gui);
    std::string shown;
    const auto r = draw_status(gui, *all.shells[0], rv_editor_pane_kind::release_controls, std::ceil(min.w), shown);
    const line now = status_of(r);
    RV_EDITOR_CHECK(now.found);
    const float width = now.x1 - now.x0;
    for (size_t passed = 0; passed < 8; ++passed) {
        rv_editor_candidate c = candidate(999999, false);
        for (size_t i = 0; i < passed; ++i) {
            rv_editor_check_set(c, i, rv_editor_check_state::passed, "ok", "env");
        }
        for (const auto building : { rv_editor_release_building::none,
                 rv_editor_release_building::candidate,
                 rv_editor_release_building::development }) {
            const std::string text = rv_editor_release_facts_make(&c, building).first_line;
            const float h = wrapped_height(gui, text, width);
            if (!now.found || h > two + 0.5f) {
                std::fprintf(stderr,
                    "  status %.1f px wide at the minimum width %.0f: '%s' needs %.1f px high, the status has %.1f\n",
                    width,
                    std::ceil(min.w),
                    text.c_str(),
                    h,
                    two);
            }
            RV_EDITOR_CHECK(now.found && h <= two + 0.5f);
        }
    }
}
