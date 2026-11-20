// S2 of the panes work, R1/R2/R19, AC1/AC2: Console Output, Runtime Log, Build Log, Search, Terminal, Files (and its
// questions), Run Config and Project draw no message block while they have nothing to say: no window of the old box
// (##notes, ##note, ##say, ##ask), the content area right under the shelf; with a message a strip stands under the area,
// the shelf, the header and the first row stay at the same pixels and the area's bottom rises by the strip; the minimum
// of the pane holds no room for a block.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "panes/rv_editor_pane_tiled.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_s15.hpp"
#include "rv_editor_test_strip.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr ImVec2 roomy(900.0f, 640.0f);

// One pane under test: how to bring it to a state with nothing to say, to the states that say something, and to draw it.
struct subject {
    std::string name;
    rv_editor_pane_kind kind = rv_editor_pane_kind::output;
    bool shelf = false;                    // a shelf over the area
    bool header = false;                   // and a table header between them (the logs)
    bool rows = true;                      // the first row is the same in the state with a message (a log's tail is not)
    std::vector<std::function<void()>> extra; // states with a message that no state without one is a pair of
    std::function<void()> quiet;           // empty: the pane has no state without a message
    std::vector<std::function<void()>> say;
    std::function<rv_editor_probe_record(float, float)> draw;
    std::function<rv_editor_shell &()> shell;
    std::function<float()> room_without_block; // the minimum height with no block and no strip: the shelf, the header, the floors
};

struct bench {
    std::vector<std::unique_ptr<s13::fixture>> logs;
    std::vector<std::unique_ptr<s15::fixture>> forms;
    std::vector<subject> subjects;

    explicit bench(rv_editor_test_imgui &gui)
    {
        add_logs(gui);
        add_terminal_and_search(gui);
        add_forms(gui);
    }

    void add_logs(rv_editor_test_imgui &gui)
    {
        for (const auto kind : { rv_editor_pane_kind::output, rv_editor_pane_kind::runtime_log, rv_editor_pane_kind::build_log }) {
            logs.push_back(std::make_unique<s13::fixture>(gui, kind));
            s13::fixture &f = *logs.back();
            subject s;
            s.name = s13::name_of(kind);
            s.kind = kind;
            s.shelf = true;
            s.header = true;
            s.rows = false;
            // Lines, no filter, nothing exported: a log with nothing to say. The exported file and the hidden lines say it.
            s.quiet = [&f] {
                f.states[1]();
                f.app().outputs[s13::pane].search[0] = '\0';
                f.app().outputs[s13::pane].exported.clear();
            };
            s.say = { [&f] {
                         f.states[1]();
                         f.app().outputs[s13::pane].exported = (f.where / "export.log").string();
                     },
                [&f] {
                    f.states[2]();
                } };
            s.draw = [&f](float w, float h) {
                return s13::settled(f, w, h);
            };
            s.shell = [&f]() -> rv_editor_shell & {
                return f.shell;
            };
            s.room_without_block = [] {
                // the shelf, the table header (a text line and 4 px, the log's own constant), a spacing and the area at its floor
                return static_cast<float>(
                    rv_editor_tiled_minimum(100.0f,
                        rv_editor_tiled_shelf_block() + ImGui::GetTextLineHeight() + 4.0f + ImGui::GetStyle().ItemSpacing.y)
                        .h);
            };
            subjects.push_back(std::move(s));
        }
    }

    void add_terminal_and_search(rv_editor_test_imgui &gui)
    {
        logs.push_back(std::make_unique<s13::fixture>(gui, rv_editor_pane_kind::terminal));
        s13::fixture &t = *logs.back();
        subject term;
        term.name = "terminal";
        term.rows = false;
        term.kind = rv_editor_pane_kind::terminal;
        term.quiet = [&t] {
            t.states[0]();
        };
        term.say = { [&t] {
                        t.states[2]();
                    },
            [&t] {
                t.states[3]();
            },
            [&t] {
                t.states[4]();
            } };
        term.draw = [&t](float w, float h) {
            return s13::settled(t, w, h);
        };
        term.shell = [&t]() -> rv_editor_shell & {
            return t.shell;
        };
        term.room_without_block = [] {
            return static_cast<float>(rv_editor_tiled_minimum(100.0f, 0.0f).h);
        };
        subjects.push_back(std::move(term));

        logs.push_back(std::make_unique<s13::fixture>(gui, rv_editor_pane_kind::search));
        s13::fixture &q = *logs.back();
        subject search;
        search.name = "search";
        search.kind = rv_editor_pane_kind::search;
        search.shelf = true;
        // Search always has a result line to show once a project is open: no state without a message.
        for (const size_t i : { size_t(0), size_t(1), size_t(2), size_t(3), size_t(4) }) {
            search.say.push_back([&q, i] {
                q.states[i]();
            });
        }
        search.draw = [&q](float w, float h) {
            return s13::settled(q, w, h);
        };
        search.shell = [&q]() -> rv_editor_shell & {
            return q.shell;
        };
        search.room_without_block = [] {
            return static_cast<float>(rv_editor_tiled_minimum(100.0f, rv_editor_tiled_shelf_block()).h);
        };
        subjects.push_back(std::move(search));
    }

    void add_form(rv_editor_test_imgui &gui,
        rv_editor_pane_kind kind,
        bool shelf,
        size_t quiet_state,
        std::vector<size_t> say_states,
        std::function<float()> room)
    {
        forms.push_back(std::make_unique<s15::fixture>(gui, kind));
        s15::fixture &f = *forms.back();
        subject s;
        s.name = s15::name_of(kind);
        s.kind = kind;
        s.shelf = shelf;
        s.quiet = [&f, quiet_state] {
            f.states[quiet_state]();
        };
        for (const size_t i : say_states) {
            s.say.push_back([&f, i] {
                f.states[i]();
            });
        }
        s.draw = [&f](float w, float h) {
            return f.settled(w, h);
        };
        s.shell = [&f]() -> rv_editor_shell & {
            return f.shell;
        };
        s.room_without_block = std::move(room);
        subjects.push_back(std::move(s));
    }

    void add_forms(rv_editor_test_imgui &gui)
    {
        // Files: 1 tree, 2 new file, 3 rename, 4 delete a file, 6 a rename that failed (5, the folder, is the card's).
        add_form(gui, rv_editor_pane_kind::files, true, 1, { 2, 3, 4, 6 }, [] {
            return static_cast<float>(rv_editor_tiled_minimum(100.0f, rv_editor_tiled_shelf_block()).h);
        });
        // Run Config: 1 clean, 3 a profile error, 4 a problem of the profile; the button row under the area stays.
        add_form(gui, rv_editor_pane_kind::run_config, false, 1, { 3, 4 }, [] {
            return static_cast<float>(rv_editor_tiled_minimum(100.0f, 0.0f).h) +
                std::ceil(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
        });
        // Project: 1 open and well, 0 no project, 2 a manifest error, 3 a tool that cannot run, 4 a settings error.
        add_form(gui, rv_editor_pane_kind::project, false, 1, { 2, 3, 4 }, [] {
            return static_cast<float>(
                rv_editor_tiled_minimum(100.0f, 0.0f, { rv_editor_tiled_floor(), rv_editor_tiled_floor() }).h);
        });
        s15::fixture &project = *forms.back();
        subjects.back().extra.push_back([&project] {
            project.states[0]();
        });
    }
};

void report(const subject &s, const char *what, const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s %s: %s\n", s.name.c_str(), what, bad.c_str());
    }
}

} // namespace

// AC1, R1: with nothing to say the pane has no window of the old message box and its first content area starts right under
// the shelf (and the table header of a log): at most the one spacing of the style between, at the minimum and roomy.
RV_EDITOR_TEST(rv_editor_test_strip_panes_with_nothing_to_say_draw_no_block)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    bench b(gui);
    float pad = 0.0f;
    float gap = 0.0f;
    float shelf_block = 0.0f;
    float frame_h = 0.0f;
    gui.frame([&] {
        pad = ImGui::GetStyle().WindowPadding.y;
        gap = ImGui::GetStyle().ItemSpacing.y;
        shelf_block = rv_editor_tiled_shelf_block();
        frame_h = ImGui::GetFrameHeight();
    });
    for (subject &s : b.subjects) {
        if (!s.quiet) {
            continue;
        }
        const auto min = stable_tiles::pane_minimum(gui, s.kind);
        for (const ImVec2 size : { ImVec2(min.w, min.h), roomy }) {
            s.quiet();
            const auto r = s.draw(size.x, size.y);
            const bool windows = strip::has_message_window(r);
            report(s, "has a message window", windows ? "yes" : "");
            RV_EDITOR_CHECK(!windows);
            const auto *area = strip::first_area(r);
            RV_EDITOR_CHECK(area != nullptr);
            if (area == nullptr) {
                continue;
            }
            // The shelf's block ends with its own spacing; a log's header is a frame at most and one spacing more.
            const float allowed = pad + (s.shelf ? shelf_block : 0.0f) + (s.header ? frame_h + gap : 0.0f) + gap + 0.5f;
            if (area->y0 > allowed) {
                std::fprintf(stderr,
                    "  %s at %.0fx%.0f: the area starts %.1f px down, %.1f at most\n",
                    s.name.c_str(),
                    size.x,
                    size.y,
                    area->y0,
                    allowed);
            }
            RV_EDITOR_CHECK(area->y0 <= allowed);
        }
    }
}

// AC2, R2: a message is a strip under the area. The shelf, the header and the first row are at the same pixels, the
// area's bottom rises by the strip and its gap, the buttons stand in the strip; what stood under the area stays.
RV_EDITOR_TEST(rv_editor_test_strip_panes_a_message_is_a_strip_that_moves_nothing_above_it)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    bench b(gui);
    float gap = 0.0f;
    float frame_h = 0.0f;
    gui.frame([&] {
        gap = ImGui::GetStyle().ItemSpacing.y;
        frame_h = ImGui::GetFrameHeight();
    });
    for (subject &s : b.subjects) {
        rv_editor_probe_record quiet;
        const bool have_quiet = static_cast<bool>(s.quiet);
        if (have_quiet) {
            s.quiet();
            quiet = s.draw(roomy.x, roomy.y);
        }
        for (size_t i = 0; i < s.say.size(); ++i) {
            s.say[i]();
            const auto said = s.draw(roomy.x, roomy.y);
            RV_EDITOR_CHECK(!strip::has_message_window(said));
            const std::string bad = strip::strip_problems(have_quiet ? &quiet : nullptr, said, roomy, gap, frame_h, s.rows);
            report(s, ("message " + std::to_string(i)).c_str(), bad);
            RV_EDITOR_CHECK(bad.empty());
        }
    }
}

// R19: the minimum of the pane is a true minimum for every state (rule 1) and static: the shelf (the header), the area at
// its floor, the strip at its tallest (three lines of text, a longer message scrolls inside; with the row of the name field
// under them in Files) and a gap, whatever the messages are. Every state, with a message or without, fits whole at it.
RV_EDITOR_TEST(rv_editor_test_strip_panes_minimum_is_the_shelf_the_floor_and_the_tallest_strip)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    bench b(gui);
    for (subject &s : b.subjects) {
        const auto min = stable_tiles::pane_minimum(gui, s.kind);
        float want = 0.0f;
        gui.frame([&] {
            want = s.room_without_block() + rv_editor_message_strip_max_height(s.kind == rv_editor_pane_kind::files) +
                ImGui::GetStyle().ItemSpacing.y;
        });
        if (std::fabs(min.h - want) > 1.5f) {
            std::fprintf(stderr, "  %s: the minimum is %.0f px high; shelf, floor, the tallest strip and a gap make %.0f\n", s.name.c_str(), min.h, want);
        }
        RV_EDITOR_CHECK(std::fabs(min.h - want) <= 1.5f);
        std::vector<std::function<void()>> states = s.say;
        states.insert(states.end(), s.extra.begin(), s.extra.end());
        if (s.quiet) {
            states.push_back(s.quiet);
        }
        for (size_t i = 0; i < states.size(); ++i) {
            const std::string bad = rv_editor_check_pane_states(gui, s.shell(), s.kind, { states[i] }, min);
            report(s, ("state " + std::to_string(i) + " at the minimum").c_str(), bad);
            RV_EDITOR_CHECK(bad.empty());
        }
    }
}

// The checks above are only as good as strip_problems: it accepts the strip widget of S1 laid out under an area the way a
// pane does it, and it rejects a strip drawn over the area or a message that moved the shelf.
RV_EDITOR_TEST(rv_editor_test_strip_panes_helper_accepts_the_widget_and_rejects_a_mess)
{
    rv_editor_test_imgui gui(true);
    constexpr rv_editor_theme theme = rv_editor_theme_olive;
    constexpr float w = 420.0f;
    constexpr float h = 300.0f;
    const auto draw = [&](const std::string &text, float shelf_extra) {
        const auto body = [&](rv_editor_probe_record &r) {
            ImGui::Dummy(ImVec2(1.0f, shelf_extra));
            ImGui::Button("shelf");
            r.item("shelf");
            const float strip_h = rv_editor_message_strip_height(text, { "Dismiss" }, ImGui::GetContentRegionAvail().x);
            (void)rv_editor_tiled_area_begin("##area", 100.0f, theme, {}, false, 0.0f, strip_h);
            rv_editor_list_row("##row", "first row", theme);
            rv_editor_tiled_area_end(theme);
            (void)rv_editor_message_strip("strip", text, rv_editor_label_kind::error, { "Dismiss" }, theme);
        };
        rv_editor_probe_draw(gui, w, body, h);
        return rv_editor_probe_draw(gui, w, body, h);
    };
    float gap = 0.0f;
    float frame_h = 0.0f;
    gui.frame([&] {
        gap = ImGui::GetStyle().ItemSpacing.y;
        frame_h = ImGui::GetFrameHeight();
    });
    const auto quiet = draw("", 0.0f);
    const auto said = draw("Build failed.", 0.0f);
    const std::string good = strip::strip_problems(&quiet, said, ImVec2(w, h), gap, frame_h);
    if (!good.empty()) {
        std::fprintf(stderr, "  helper on the widget: %s\n", good.c_str());
    }
    RV_EDITOR_CHECK(good.empty());
    RV_EDITOR_CHECK(!strip::strip_problems(&quiet, quiet, ImVec2(w, h), gap, frame_h).empty()); // no strip at all
    RV_EDITOR_CHECK(!strip::strip_problems(&quiet, draw("Build failed.", 20.0f), ImVec2(w, h), gap, frame_h).empty()); // moved
}
