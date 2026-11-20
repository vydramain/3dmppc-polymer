// S3, R1/R2, AC1/AC2: the panes and pages that are not logs, search, terminal, files, run configuration or project (Scene,
// Hierarchy, Inspector, Assets, Build Result, Findings, Export, Observe and the pages) say things in a strip under their
// content area and keep no message box: with nothing to say nothing is drawn and the area starts right under the shelf; a
// message makes a strip at the bottom and nothing above it moves.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"
#include "rv_editor_test_s16.hpp"
#include "rv_editor_test_s17_states.hpp"
#include "rv_editor_test_s3.hpp"
#include "rv_editor_test_strip.hpp"
#include "ui/rv_editor_widgets_message.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr float roomy_w = 760.0f;
constexpr float roomy_h = 560.0f;

// The Export pane keeps a 2-line description of the chosen kind, which is a text window of its own and not a message.
int allowed_boxes(rv_editor_pane_kind kind)
{
    return kind == rv_editor_pane_kind::export_ ? 1 : 0;
}

// One state of one pane, drawn: what a pane's fixture sets up and settles.
struct source {
    const char *name;
    rv_editor_pane_kind kind;
    size_t states;
    std::function<rv_editor_probe_record(size_t, float, float)> draw;
};

// The panes of S3 through the three fixtures they already have; `hold` keeps each fixture alive.
struct sources {
    std::vector<std::unique_ptr<s16::fx>> s16s;
    std::vector<std::unique_ptr<s15::fixture>> s15s;
    std::vector<std::unique_ptr<s17::fixture>> s17s;
    std::vector<source> all;

    explicit sources(rv_editor_test_imgui &gui)
    {
        for (const auto kind : s16::kinds()) {
            auto &f = *s16s.emplace_back(std::make_unique<s16::fx>(gui, kind));
            all.push_back({ s16::name_of(kind), kind, f.states.size(), [&f](size_t i, float w, float h) {
                               f.states[i]();
                               return s16::settled(f, w, h);
                           } });
        }
        for (const auto kind : { rv_editor_pane_kind::assets, rv_editor_pane_kind::findings }) {
            auto &f = *s15s.emplace_back(std::make_unique<s15::fixture>(gui, kind));
            all.push_back({ s15::name_of(kind), kind, f.states.size(), [&f](size_t i, float w, float h) {
                               f.states[i]();
                               return f.settled(w, h);
                           } });
        }
        for (const auto kind :
            { rv_editor_pane_kind::export_, rv_editor_pane_kind::observe, rv_editor_pane_kind::build_result }) {
            auto &f = *s17s.emplace_back(std::make_unique<s17::fixture>(gui, kind));
            all.push_back({ s17::name_of(kind), kind, f.states.size(), [&f](size_t i, float w, float h) {
                               f.states[i].set();
                               return s17::settled(f, w, h);
                           } });
        }
    }
    const source &of(rv_editor_pane_kind kind) const
    {
        for (const auto &s : all) {
            if (s.kind == kind) {
                return s;
            }
        }
        return all.front();
    }
};

void report(const char *what, size_t state, const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s, state %zu: %s\n", what, state, bad.c_str());
    }
}

} // namespace

// AC1: every one of the 34 kinds in the base state of a fresh shell is whole at its minimum and draws no message box (a
// message there is a strip, which is not a window); session and candidate (S8, S10) have none either.
RV_EDITOR_TEST(rv_editor_test_s3_no_kind_draws_a_message_box_in_its_base_state)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    for (uint32_t k = 0; k <= static_cast<uint32_t>(rv_editor_pane_kind_last); ++k) {
        const auto kind = static_cast<rv_editor_pane_kind>(k);
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        rv_editor_shell shell;
        const std::string bad = rv_editor_check_pane_states(gui, shell, kind, { [] {} }, min);
        if (!bad.empty()) {
            std::fprintf(stderr, "  kind %u at %.0fx%.0f: %s\n", k, min.w, min.h, bad.c_str());
        }
        RV_EDITOR_CHECK(bad.empty());
        const auto r = rv_editor_probe_draw(
            gui,
            min.w,
            [&](rv_editor_probe_record &) {
                rv_editor_shell_pane(&shell, 1, kind, stable_tiles::theme);
            },
            min.h);
        const int boxes = s3::boxes(r);
        if (boxes > allowed_boxes(kind)) {
            std::fprintf(stderr, "  kind %u draws %d message boxes\n", k, boxes);
        }
        RV_EDITOR_CHECK(boxes <= allowed_boxes(kind));
    }
}

// AC1: in no state of Scene, Hierarchy, Inspector, Assets, Findings, Export, Observe and Build Result, at the minimum and in
// a roomy tile, is there a message box; and the whole of each is inside its tile (strips and areas never leave it).
RV_EDITOR_TEST(rv_editor_test_s3_no_state_of_the_panes_draws_a_message_box)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    sources src(gui);
    for (const auto &s : src.all) {
        const auto min = stable_tiles::pane_minimum(gui, s.kind);
        for (size_t i = 0; i < s.states; ++i) {
            for (const auto size : { ImVec2(min.w, min.h), ImVec2(roomy_w, roomy_h) }) {
                const auto r = s.draw(i, size.x, size.y);
                const int n = s3::boxes(r);
                if (n > allowed_boxes(s.kind)) {
                    std::fprintf(stderr, "  %s, state %zu at %.0fx%.0f: %d message boxes\n", s.name, i, size.x, size.y, n);
                }
                RV_EDITOR_CHECK(n <= allowed_boxes(s.kind));
                const std::string bad = rv_editor_fits_whole(r, ImRect(0, 0, size.x, size.y));
                report(s.name, i, bad);
                RV_EDITOR_CHECK(bad.empty());
            }
        }
    }
}

// AC1: with nothing to say the content starts right under the shelf (the gap is at most the item spacing, or the window's
// padding and the spacing when there is no shelf): Scene with a scene open (state 2), Inspector with an object selected
// (2), Findings with a project and no error (1), Assets with an empty project (1), Hierarchy with a tree (1), Observe
// with no session (1), Build Result idle (1); and Export while a job runs (5, "the steps area says what runs") draws
// no strip and nothing but its description of the kind.
RV_EDITOR_TEST(rv_editor_test_s3_with_nothing_to_say_the_content_is_right_under_the_shelf)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    sources src(gui);
    struct want {
        rv_editor_pane_kind kind;
        size_t state;
    };
    float spacing = 0.0f;
    float padding = 0.0f;
    gui.frame([&] {
        spacing = ImGui::GetStyle().ItemSpacing.y;
        padding = ImGui::GetStyle().WindowPadding.y;
    });
    for (const want w : { want{ rv_editor_pane_kind::scene, 2 },
             want{ rv_editor_pane_kind::inspector, 2 },
             want{ rv_editor_pane_kind::findings, 1 },
             want{ rv_editor_pane_kind::assets, 1 },
             want{ rv_editor_pane_kind::hierarchy, 1 },
             want{ rv_editor_pane_kind::observe, 1 },
             want{ rv_editor_pane_kind::build_result, 1 },
             want{ rv_editor_pane_kind::export_, 5 } }) {
        const auto &s = src.of(w.kind);
        const auto r = s.draw(w.state, roomy_w, roomy_h);
        RV_EDITOR_CHECK(s3::boxes(r) <= allowed_boxes(w.kind));
        // Nothing is reserved at the bottom either: no strip.
        RV_EDITOR_CHECK(s3::strip_of(r) == nullptr);
        if (w.kind == rv_editor_pane_kind::export_) {
            continue; // its description is a window between the form and the area by design
        }
        const rv_editor_probe_entry *area = w.kind == rv_editor_pane_kind::scene ?
            s16::find(r, "##viewport") :
            (s3::areas(r).empty() ? nullptr : s3::areas(r)[0]);
        RV_EDITOR_CHECK(area != nullptr);
        if (area == nullptr) {
            std::fprintf(stderr, "  %s: no content area\n", s.name);
            continue;
        }
        const float gap = s3::gap_above(r, *area);
        if (gap > spacing + padding + 1.0f) {
            std::fprintf(stderr, "  %s: %.1f px between what is above and the area (spacing %.1f)\n", s.name, gap, spacing);
        }
        RV_EDITOR_CHECK(gap <= spacing + padding + 1.0f);
    }
}

// AC2: Inspector and Findings: the refusal, the read-only banner and the error come up as a strip under the area; the
// shelf, the first row and everything above the area stay at the same coordinates, the area loses height at its bottom.
RV_EDITOR_TEST(rv_editor_test_s3_a_message_is_a_strip_under_the_area_and_moves_nothing_above_it)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    sources src(gui);
    struct pair {
        rv_editor_pane_kind kind;
        size_t quiet;
        size_t said;
    };
    float gap = 0.0f;
    float frame_h = 0.0f;
    gui.frame([&] {
        gap = ImGui::GetStyle().ItemSpacing.y;
        frame_h = ImGui::GetFrameHeight();
    });
    for (const pair p : { pair{ rv_editor_pane_kind::inspector, 2, 3 },
             pair{ rv_editor_pane_kind::inspector, 2, 4 },
             pair{ rv_editor_pane_kind::findings, 1, 2 } }) {
        const auto &s = src.of(p.kind);
        const auto quiet = s.draw(p.quiet, roomy_w, roomy_h);
        const auto said = s.draw(p.said, roomy_w, roomy_h);
        const std::string bad = s3::strip_appears(quiet, said, roomy_h);
        report(s.name, p.said, bad);
        RV_EDITOR_CHECK(bad.empty());
        // And S2's rule: the area's bottom rose by the strip and its gap, the strip is under the area inside the tile.
        const std::string rule = strip::strip_problems(&quiet, said, ImVec2(roomy_w, roomy_h), gap, frame_h, false);
        report(s.name, p.said, rule);
        RV_EDITOR_CHECK(rule.empty());
    }
}

// AC2: Hierarchy's target is a placeholder, not a message: no strip in the tree and selection states; the refusal of a move
// (with Dismiss) is a strip under the tree and the shelf and the form stay at the same coordinates.
RV_EDITOR_TEST(rv_editor_test_s3_hierarchy_says_the_target_and_the_move_note_in_a_strip_under_the_tree)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    sources src(gui);
    const auto &s = src.of(rv_editor_pane_kind::hierarchy);
    // 1 tree, 4 move note, 5 selection.
    const auto tree = s.draw(1, roomy_w, roomy_h);
    for (const size_t state : { size_t(1), size_t(4), size_t(5) }) {
        const auto r = s.draw(state, roomy_w, roomy_h);
        RV_EDITOR_CHECK(s3::boxes(r) == 0);
        const auto *strip = s3::strip_of(r);
        RV_EDITOR_CHECK((strip != nullptr) == (state == 4));
        if (strip == nullptr && state == 4) {
            std::fprintf(stderr, "  hierarchy state 4: no strip under the tree\n");
        }
        const std::string moved = s3::above_area_same(tree, r);
        report("hierarchy", state, moved);
        RV_EDITOR_CHECK(moved.empty());
        RV_EDITOR_CHECK(strip == nullptr || strip->y1 <= roomy_h + 0.5f);
    }
}

// AC2: Observe, Assets and Build Result: a real message is a strip under their areas; a placeholder is no message.
RV_EDITOR_TEST(rv_editor_test_s3_observe_assets_and_build_result_say_things_in_a_strip)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    sources src(gui);
    // The real messages: observe 0 (no project, Open Project) and 2 (the session ended), assets 0, build result 4 (failed).
    struct says {
        rv_editor_pane_kind kind;
        size_t state;
    };
    for (const says w : { says{ rv_editor_pane_kind::observe, 0 },
             says{ rv_editor_pane_kind::observe, 2 },
             says{ rv_editor_pane_kind::assets, 0 },
             says{ rv_editor_pane_kind::build_result, 4 } }) {
        const auto &s = src.of(w.kind);
        const auto r = s.draw(w.state, roomy_w, 700.0f);
        RV_EDITOR_CHECK(s3::boxes(r) == 0);
        const auto *strip = s3::strip_of(r);
        const auto all = s3::areas(r);
        RV_EDITOR_CHECK(strip != nullptr && !all.empty());
        if (strip == nullptr || all.empty()) {
            std::fprintf(stderr, "  %s, state %zu: no strip under the areas\n", s.name, w.state);
            continue;
        }
        RV_EDITOR_CHECK(strip->y1 <= 700.0f + 0.5f);
        // The strip is as wide as the pane's content: one row of the text and its buttons, not a stub.
        RV_EDITOR_CHECK(strip->x1 - strip->x0 > roomy_w / 2.0f);
    }
    // The placeholders ("no session", "nothing built", "click an asset", "nothing selected") are no messages: no strip, no
    // block; an empty area says it in its own empty row.
    for (const says w : { says{ rv_editor_pane_kind::observe, 1 },
             says{ rv_editor_pane_kind::assets, 1 },
             says{ rv_editor_pane_kind::assets, 3 },
             says{ rv_editor_pane_kind::build_result, 1 },
             says{ rv_editor_pane_kind::hierarchy, 1 },
             says{ rv_editor_pane_kind::hierarchy, 5 },
             says{ rv_editor_pane_kind::inspector, 1 } }) {
        const auto &s = src.of(w.kind);
        const auto r = s.draw(w.state, roomy_w, 700.0f);
        if (s3::strip_of(r) != nullptr || s3::boxes(r) != 0) {
            std::fprintf(stderr, "  %s, state %zu: a placeholder is drawn as a message\n", s.name, w.state);
        }
        RV_EDITOR_CHECK(s3::strip_of(r) == nullptr && s3::boxes(r) == 0);
    }
}

// AC1/AC2: the New Scene question in the Scene pane is no message box either: its name field, the C++ choice and the
// Create and Cancel buttons are all there, whole, in the question's state (5), and the "No scene" state (1) has its Create
// Scene button and a strip.
RV_EDITOR_TEST(rv_editor_test_s3_scene_question_and_no_scene_say_things_without_a_box)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    sources src(gui);
    const auto &s = src.of(rv_editor_pane_kind::scene);
    const auto ask = s.draw(5, roomy_w, roomy_h);
    RV_EDITOR_CHECK(s3::boxes(ask) == 0);
    RV_EDITOR_CHECK(s16::find(ask, "##new_scene_name") != nullptr);
    RV_EDITOR_CHECK(s16::find(ask, rv_editor_text("shell_menu_scene.create_button")) != nullptr);
    RV_EDITOR_CHECK(s16::find(ask, rv_editor_text("shell_menu_scene.cancel_button")) != nullptr);
    RV_EDITOR_CHECK(!s13::has_ask_place(ask));
    const auto none = s.draw(1, roomy_w, roomy_h);
    RV_EDITOR_CHECK(s3::boxes(none) == 0);
    RV_EDITOR_CHECK(s16::find(none, rv_editor_text("pane_scene.create_scene")) != nullptr);
    RV_EDITOR_CHECK(s3::strip_of(none) != nullptr);
}

// AC1: the Export description (a window of its own) is the only text window Export may draw; with a job running and with a
// result or failure shown the message is a strip.
RV_EDITOR_TEST(rv_editor_test_s3_export_says_the_result_and_the_failure_in_a_strip)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    sources src(gui);
    const auto &s = src.of(rv_editor_pane_kind::export_);
    // 6 succeeded, 7 failed, 8 cancelled.
    for (const size_t state : { size_t(6), size_t(7), size_t(8) }) {
        const auto r = s.draw(state, roomy_w, roomy_h);
        RV_EDITOR_CHECK(s3::boxes(r) <= 1);
        const auto *strip = s3::strip_of(r);
        RV_EDITOR_CHECK(strip != nullptr);
        if (strip == nullptr) {
            std::fprintf(stderr, "  export state %zu: no strip\n", state);
        }
    }
}

// R19, S2's rule: the minimum of a pane with a strip is static, the same in every state: what the pane needs with nothing to
// say (the shelf, the form, the area at its floor) plus the tallest strip (three lines, one in Findings; a longer text scrolls
// inside it) and the gap. The least height at which the quiet state fits whole is found by halving; the minimum is that and the
// strip.
RV_EDITOR_TEST(rv_editor_test_s3_the_minimum_is_the_quiet_state_the_tallest_strip_and_a_gap)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    sources src(gui);
    struct quiet {
        rv_editor_pane_kind kind;
        size_t state;
    };
    float strip_max = 0.0f;
    float one_line = 0.0f; // Findings sits in the logs' tile: its strip is one line (S13.1a.2)
    float gap = 0.0f;
    gui.frame([&] {
        strip_max = rv_editor_message_strip_max_height(false);
        one_line = rv_editor_message_strip_max_height(false, 1);
        gap = ImGui::GetStyle().ItemSpacing.y;
    });
    for (const quiet q : { quiet{ rv_editor_pane_kind::inspector, 2 },
             quiet{ rv_editor_pane_kind::findings, 1 },
             quiet{ rv_editor_pane_kind::assets, 1 },
             quiet{ rv_editor_pane_kind::hierarchy, 1 },
             quiet{ rv_editor_pane_kind::observe, 1 },
             quiet{ rv_editor_pane_kind::build_result, 1 } }) {
        const auto &s = src.of(q.kind);
        const auto min = stable_tiles::pane_minimum(gui, q.kind);
        const auto fits = [&](float h) {
            return rv_editor_fits_whole(s.draw(q.state, min.w, h), ImRect(0, 0, min.w, h)).empty();
        };
        float lo = 0.0f;
        float hi = min.h;
        while (hi - lo > 1.0f) {
            const float mid = std::floor((lo + hi) / 2.0f);
            (fits(mid) ? hi : lo) = mid;
        }
        const float want = hi + (q.kind == rv_editor_pane_kind::findings ? one_line : strip_max) + gap;
        if (std::fabs(min.h - want) > 1.5f) {
            std::fprintf(stderr,
                "  %s: the minimum is %.0f px high; the quiet state (%.0f), the tallest strip and a gap make %.0f\n",
                s.name,
                min.h,
                hi,
                want);
        }
        RV_EDITOR_CHECK(std::fabs(min.h - want) <= 1.5f);
    }
}

// S13.1a.2: Findings sits in the logs' tile, so its strip is one line high: a long error scrolls inside it and no text is cut
// with an ellipsis; the area's bottom rises by that one line and the gap.
RV_EDITOR_TEST(rv_editor_test_s3_the_findings_strip_is_one_line_high_and_scrolls)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    sources src(gui);
    const auto &s = src.of(rv_editor_pane_kind::findings);
    float gap = 0.0f;
    float one = 0.0f;
    gui.frame([&] {
        gap = ImGui::GetStyle().ItemSpacing.y;
        one = rv_editor_message_strip_max_height(false, 1);
    });
    const auto quiet = s.draw(1, roomy_w, roomy_h);
    const auto said = s.draw(2, roomy_w, roomy_h); // "cannot write: " and 300 characters
    const float height = strip::strip_height(&quiet, said);
    if (std::fabs(height - one) > 1.5f) {
        std::fprintf(stderr, "  findings: the strip is %.1f px high, one line makes %.1f\n", height, one);
    }
    RV_EDITOR_CHECK(std::fabs(height - one) <= 1.5f);
    const auto *a = strip::last_area(quiet);
    const auto *c = strip::last_area(said);
    RV_EDITOR_CHECK(a != nullptr && c != nullptr);
    if (a != nullptr && c != nullptr) {
        RV_EDITOR_CHECK(std::fabs((a->y1 - c->y1) - (one + gap)) <= 1.5f);
    }
    for (const auto &e : said.entries) {
        RV_EDITOR_CHECK(e.label.substr(0, e.label.find("##")).find("...") == std::string::npos);
    }
}
