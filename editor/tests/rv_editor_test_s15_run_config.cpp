// S15, R13, AC14: Run Config is a list of profiles in a content area and the form (S6), with no message block: a label never
// meets its field, a value or a path is shown whole (a path inside the project from its root, "src/..."), the error
// line of a field and the button row keep their places with an error and without one.

#include <cstdio>
#include <fstream>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"
#include "rv_editor_test_strip.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s15;

namespace
{

constexpr float roomy_w = 900.0f;
constexpr float roomy_h = 640.0f;
// Clean, long values, a profile error, a problem of the profile (the fixture's states 1-4; 0 is no project).
constexpr size_t clean_state = 1;
constexpr size_t long_state = 2;
constexpr size_t error_state = 3;
constexpr size_t problem_state = 4;

std::vector<std::string> anchors()
{
    std::vector<std::string> a;
    for (const char *key :
        { "pane_run_config.apply_label", "pane_run_config.apply_restart_label", "pane_run_config.revert_label" }) {
        a.emplace_back(rv_editor_text(key));
    }
    return a;
}

// The minimum width and a roomy height, and the roomy size: the probe sees a field only where the area scrolled to it,
// so labels, fields and values are checked with room in height (the whole-at-the-minimum check is the fits test's).
std::vector<ImVec2> form_sizes(rv_editor_test_imgui &gui, rv_editor_pane_kind kind)
{
    const auto min = stable_tiles::pane_minimum(gui, kind);
    if (min.w <= 0.0f) {
        return { ImVec2(roomy_w, roomy_h) };
    }
    return { ImVec2(min.w, roomy_h), ImVec2(roomy_w, roomy_h) };
}

// The record without what is inside a content area: a state's values and errors scroll there, and the area's own frame
// is as high as the strip under it leaves; what is above and under the area stays.
rv_editor_probe_record frames_of(const rv_editor_probe_record &r)
{
    rv_editor_probe_record out;
    out.content_height = r.content_height;
    const auto *area = strip::first_area(r);
    for (const auto &e : r.entries) {
        const bool in_area = area != nullptr && e.y0 >= area->y0 - 0.5f && e.y0 < area->y1 - 0.5f;
        if (!e.in_scroll && !in_area) {
            out.entries.push_back(e);
        }
    }
    return out;
}

} // namespace

// AC14: with labels of 30 characters no field starts over its label: the fields are right of the widest label (or under
// it), and the fields start in one column.
RV_EDITOR_TEST(rv_editor_test_s15_run_config_label_never_meets_its_field)
{
    const fs::path dir = fs::temp_directory_path() / ("rv_editor_s15_run_labels_" + std::to_string(getpid()));
    fs::create_directories(dir);
    const fs::path file = dir / "texts.toml";
    const std::string longest = "WorkingWorkingWorkingWorkingWo";
    std::ofstream(file) << "[pane_run_config]\nname_label = \"NameNameNameNameNameNameNameNa\"\n"
                           "runtime_label = \"RuntimeRuntimeRuntimeRuntimeRu\"\n"
                           "working_dir_label = \"" +
            longest +
            "\"\n"
            "memory_card_label = \"MemoryMemoryMemoryMemoryMemory\"\n";
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load(file, error) == 0);
    rv_editor_test_imgui gui(true);
    float label_w = 0.0f;
    float pad = 0.0f;
    gui.frame([&] {
        label_w = ImGui::CalcTextSize(longest.c_str()).x;
        pad = ImGui::GetStyle().WindowPadding.x;
    });
    {
        fixture f(gui, rv_editor_pane_kind::run_config);
        f.states[clean_state]();
        const auto r = f.settled(roomy_w, roomy_h);
        const rv_editor_probe_entry *first = nullptr;
        for (const char *label : { "NameNameNameNameNameNameNameNa",
                 "RuntimeRuntimeRuntimeRuntimeRu",
                 longest.c_str(),
                 "MemoryMemoryMemoryMemoryMemory" }) {
            const auto *field = find_id(r, std::string("##") + label);
            RV_EDITOR_CHECK(field != nullptr);
            if (field == nullptr) {
                continue;
            }
            first = first == nullptr ? field : first;
            RV_EDITOR_CHECK(field->x0 == first->x0);
            RV_EDITOR_CHECK(field->x0 >= pad + label_w - 1.0f);
        }
    }
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    fs::remove_all(dir);
}

// AC14: a path inside the project shows from its root and whole, even in the smallest tile (the form's area scrolls
// sideways to it): the field is as wide as the path, which starts with "src/" and ends with its last letter.
RV_EDITOR_TEST(rv_editor_test_s15_run_config_paths_are_whole_and_from_the_root)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::run_config);
    RV_EDITOR_CHECK(stable_tiles::pane_minimum(gui, f.kind).w > 0.0f); // a path must show whole at the minimum
    float pad = 0.0f;
    gui.frame([&] {
        pad = ImGui::GetStyle().FramePadding.x;
    });
    const std::pair<const char *, char> fields[] = { { "##Runtime", 'Z' }, { "##Working dir", 'J' }, { "##Memory card", 'X' } };
    for (const ImVec2 size : form_sizes(gui, f.kind)) {
        f.states[long_state]();
        const auto r = f.settled(size.x, size.y);
        for (const auto &[id, tail] : fields) {
            const auto *e = find_id(r, id);
            RV_EDITOR_CHECK(e != nullptr);
            const rv_editor_path_probe *p = e != nullptr ? rv_editor_path_probe_of(e->id) : nullptr;
            RV_EDITOR_CHECK(p != nullptr);
            if (e == nullptr || p == nullptr) {
                continue;
            }
            const std::string expected = fs::path(p->tooltip).lexically_relative(f.where).string();
            if (p->shown != expected || expected.compare(0, 4, "src/") != 0 || p->shown.back() != tail) {
                std::fprintf(stderr,
                    "  run_config %s at %.0fx%.0f: shows '%s', expected '%s'\n",
                    id,
                    size.x,
                    size.y,
                    p->shown.c_str(),
                    expected.c_str());
            }
            RV_EDITOR_CHECK(p->shown == expected && expected.compare(0, 4, "src/") == 0 && p->shown.back() == tail);
            float text_w = 0.0f;
            gui.frame([&] {
                text_w = ImGui::CalcTextSize(p->shown.c_str()).x;
            });
            if (e->x1 - e->x0 < text_w + 2.0f * pad - 0.5f) {
                std::fprintf(stderr,
                    "  run_config %s at %.0fx%.0f: the field is %.0f px, the path %.0f\n",
                    id,
                    size.x,
                    size.y,
                    e->x1 - e->x0,
                    text_w);
            }
            RV_EDITOR_CHECK(e->x1 - e->x0 >= text_w + 2.0f * pad - 0.5f);
        }
    }
}

// R13, AC14: a path outside the project is shown whole, not cut by the field: the field is as wide as the path and the
// area scrolls to its end, at the minimum width and roomy.
RV_EDITOR_TEST(rv_editor_test_s15_run_config_a_path_outside_the_project_is_shown_whole)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::run_config);
    float pad = 0.0f;
    gui.frame([&] {
        pad = ImGui::GetStyle().FramePadding.x;
    });
    for (const ImVec2 size : form_sizes(gui, f.kind)) {
        f.states[problem_state]();
        const auto r = f.settled(size.x, size.y);
        const auto *e = find_id(r, "##Runtime");
        const rv_editor_path_probe *p = e != nullptr ? rv_editor_path_probe_of(e->id) : nullptr;
        RV_EDITOR_CHECK(e != nullptr && p != nullptr);
        if (e == nullptr || p == nullptr) {
            continue;
        }
        float text_w = 0.0f;
        gui.frame([&] {
            text_w = ImGui::CalcTextSize(p->tooltip.c_str()).x;
        });
        if (p->shown != p->tooltip || e->x1 - e->x0 < text_w + 2.0f * pad - 0.5f) {
            std::fprintf(stderr,
                "  run_config outside path at %.0fx%.0f: shows '%s' of '%s', the field is %.0f px, the path %.0f\n",
                size.x,
                size.y,
                p->shown.c_str(),
                p->tooltip.c_str(),
                e->x1 - e->x0,
                text_w);
        }
        RV_EDITOR_CHECK(p->shown == p->tooltip && p->shown.compare(0, 13, "/nonexistent/") == 0);
        RV_EDITOR_CHECK(e->x1 - e->x0 >= text_w + 2.0f * pad - 0.5f);
    }
}

// AC14, R2: a profile error and the problem of the edited profile are the strip's text; the fields, the error line under
// Runtime and the three buttons are where they are without them, and the line is reserved under Runtime. The clean pane
// has no window of the old message box.
RV_EDITOR_TEST(rv_editor_test_s15_run_config_error_and_buttons_keep_their_places)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::run_config);
    float line = 0.0f;
    float gap = 0.0f;
    float frame_h = 0.0f;
    gui.frame([&] {
        line = ImGui::GetTextLineHeight();
        gap = ImGui::GetStyle().ItemSpacing.y;
        frame_h = ImGui::GetFrameHeight();
    });
    for (const ImVec2 size : form_sizes(gui, f.kind)) {
        const auto clean = snap(f, clean_state, size.x, size.y);
        RV_EDITOR_CHECK(!strip::has_message_window(clean));
        for (const size_t state : { error_state, problem_state }) {
            const auto r = snap(f, state, size.x, size.y);
            const std::string moved = same_items(frames_of(clean), frames_of(r), anchors());
            if (!moved.empty()) {
                std::fprintf(stderr, "  run_config state %zu at %.0fx%.0f: %s\n", state, size.x, size.y, moved.c_str());
            }
            RV_EDITOR_CHECK(moved.empty());
            RV_EDITOR_CHECK(!strip::has_message_window(r));
            const std::string bad = strip::strip_problems(&clean, r, size, gap, frame_h); // the error is the strip's text
            if (!bad.empty()) {
                std::fprintf(stderr, "  run_config state %zu at %.0fx%.0f: %s\n", state, size.x, size.y, bad.c_str());
            }
            RV_EDITOR_CHECK(bad.empty());
        }
        const auto *runtime = find_id(clean, "##Runtime");
        const auto *cwd = find_id(clean, "##Working dir");
        RV_EDITOR_CHECK(runtime != nullptr && cwd != nullptr);
        if (runtime != nullptr && cwd != nullptr) {
            RV_EDITOR_CHECK(cwd->y0 - runtime->y1 >= line); // the error line of Runtime is there with no error
        }
        const auto *apply = find_text(clean, rv_editor_text("pane_run_config.apply_label"));
        const auto *restart = find_text(clean, rv_editor_text("pane_run_config.apply_restart_label"));
        const auto *revert = find_text(clean, rv_editor_text("pane_run_config.revert_label"));
        RV_EDITOR_CHECK(apply != nullptr && restart != nullptr && revert != nullptr);
        if (apply != nullptr && restart != nullptr && revert != nullptr) {
            RV_EDITOR_CHECK(apply->y0 == restart->y0 && restart->y0 == revert->y0); // one row, never wrapped
            RV_EDITOR_CHECK(apply->x1 <= restart->x0 && restart->x1 <= revert->x0);
        }
    }
}

// R10, R9: the profiles are rows of a list in a content area, whole, one each, beside or above the form.
RV_EDITOR_TEST(rv_editor_test_s15_run_config_profiles_are_a_list_in_an_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::run_config);
    const auto r = snap(f, clean_state, roomy_w, roomy_h);
    int rows = 0;
    for (const auto *a : areas(r)) {
        rows += labelled_in(r, *a, "###row");
    }
    RV_EDITOR_CHECK(rows == 3); // Default, the second with the long name, the third
    RV_EDITOR_CHECK(!has_ask_place(r));
}
