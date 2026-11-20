// S15.2, R24: a button of the message box can be disabled with a reason, and keeps its place. The widget takes one state
// per button; Look for Tools Again (while a build runs), Stop (nothing is playing) and the Ok and Cancel of a Files
// question (while its request waits) give theirs.

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"
#include "ui/rv_editor_sound.hpp"
#include "ui/rv_editor_widgets_message.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s15;
using dialog = rv_editor_files_view::rv_editor_files_dialog;

namespace
{

constexpr rv_editor_theme box_theme = rv_editor_theme_olive;
constexpr float roomy_w = 700.0f;
constexpr float roomy_h = 420.0f;
constexpr const char *reason_text = "Not now because of a reason";

// The entry of the button `label` (not a window or an item inside one).
const rv_editor_probe_entry *button_of(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label == label) {
            return &e;
        }
    }
    return nullptr;
}

// True when the tooltip window shown now draws `reason` glyph for glyph (a size would match any text of the same length).
// A glyph is a quad of four vertices whose first carries the glyph's uv; the border and background may sit before or
// after the text, so the search starts at any vertex.
bool tooltip_says(rv_editor_test_imgui &gui, const std::string &reason)
{
    const ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    if (tip == nullptr || !tip->WasActive) {
        return false;
    }
    bool match = false;
    gui.frame([&] {
        std::vector<ImVec2> v;
        for (int i = 0; i < tip->DrawList->VtxBuffer.Size; ++i) {
            v.push_back(tip->DrawList->VtxBuffer[i].uv);
        }
        std::vector<ImVec2> want;
        for (const char c : reason) {
            if (c != ' ') {
                const ImFontGlyph *g = ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(c));
                want.emplace_back(g->U0, g->V0);
            }
        }
        for (size_t i = 0; !want.empty() && !match && i + 4 * (want.size() - 1) < v.size(); ++i) {
            bool same = true;
            for (size_t k = 0; same && k < want.size(); ++k) {
                const ImVec2 &p = v[i + 4 * k];
                same = std::abs(want[k].x - p.x) < 1e-6f && std::abs(want[k].y - p.y) < 1e-6f;
            }
            match = same;
        }
    });
    return match;
}

// Pointer over the middle of `e` for longer than the tooltip delay, drawing with `draw`.
template <typename Draw>
void hover(const rv_editor_probe_entry &e, Draw draw)
{
    ImGui::GetIO().AddMousePosEvent((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
    for (int i = 0; i < 12; ++i) {
        ImGui::GetIO().DeltaTime = 0.1f;
        draw();
    }
}

void away(rv_editor_test_imgui &gui)
{
    ImGui::GetIO().AddMousePosEvent(-1000.0f, -1000.0f);
    gui.frame([] {}, roomy_w, roomy_h);
    gui.frame([] {}, roomy_w, roomy_h);
}

// A press and a release over `e`, drawing with `draw`.
template <typename Draw>
void press(const rv_editor_probe_entry &e, Draw draw)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
    io.DeltaTime = 1.0f / 60.0f;
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
}

bool same_rect_of(const rv_editor_probe_entry &a, const rv_editor_probe_entry &b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

// A fixture state drawn, then hovering the button `label` and asking whether the tooltip gives `reason`.
bool says_on_hover(fixture &f,
    size_t state,
    const std::string &label,
    const std::string &reason,
    const std::function<void()> &extra = {})
{
    f.states[state]();
    if (extra) {
        extra();
    }
    const auto r = f.settled(roomy_w, roomy_h);
    const auto *b = button_of(r, label);
    RV_EDITOR_CHECK(b != nullptr);
    if (b == nullptr) {
        return false;
    }
    hover(*b, [&] {
        f.draw(roomy_w, roomy_h);
    });
    const bool says = tooltip_says(f.gui, reason);
    away(f.gui);
    return says;
}

} // namespace

// The box with one state per button, drawn in a small window.
RV_EDITOR_TEST(rv_editor_test_s15_box_button_with_a_reason_keeps_its_place_and_says_why)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const std::vector<std::string> buttons = { "OK", "Cancel" };
    const std::vector<rv_editor_state> states = { rv_editor_state{}, rv_editor_state{ rv_editor_look::live, reason_text } };
    int pressed = -1;
    const auto draw = [&](const std::vector<rv_editor_state> *with) {
        return rv_editor_probe_draw(
            gui,
            roomy_w,
            [&](rv_editor_probe_record &) {
                const int p = rv_editor_message_box("ask",
                    "A question",
                    rv_editor_label_kind::normal,
                    buttons,
                    box_theme,
                    0.0f,
                    nullptr,
                    nullptr,
                    nullptr,
                    with);
                pressed = pressed < 0 ? p : pressed;
            },
            120.0f);
    };
    for (int i = 0; i < 3; ++i) {
        draw(nullptr);
    }
    const auto active = draw(nullptr);
    for (int i = 0; i < 3; ++i) {
        draw(&states);
    }
    const auto mixed = draw(&states);
    const auto *a = button_of(active, "Cancel");
    const auto *m = button_of(mixed, "Cancel");
    RV_EDITOR_CHECK(a != nullptr && m != nullptr);
    if (a == nullptr || m == nullptr) {
        return;
    }
    RV_EDITOR_CHECK(same_rect_of(*a, *m));
    RV_EDITOR_CHECK(button_of(mixed, "OK") != nullptr && same_rect_of(*button_of(active, "OK"), *button_of(mixed, "OK")));
    // The hover says why.
    const rv_editor_probe_entry cancel = *m;
    hover(cancel, [&] {
        draw(&states);
    });
    RV_EDITOR_CHECK(tooltip_says(gui, reason_text));
    away(gui);
    // The disabled button returns nothing; the live one still does.
    pressed = -1;
    press(cancel, [&] {
        draw(&states);
    });
    RV_EDITOR_CHECK(pressed == -1);
    away(gui);
    pressed = -1;
    press(*button_of(mixed, "OK"), [&] {
        draw(&states);
    });
    RV_EDITOR_CHECK(pressed == 0);
    away(gui);
    // The active button says nothing.
    hover(*a, [&] {
        draw(nullptr);
    });
    RV_EDITOR_CHECK(!tooltip_says(gui, reason_text));
}

// Project: Look for Tools Again is disabled while a build runs, with the reason; it keeps its place. The button stands in the
// message strip, so the project has a message (a manifest error) in both states.
RV_EDITOR_TEST(rv_editor_test_s15_project_look_for_tools_is_disabled_while_a_build_runs)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::project);
    const std::string label = rv_editor_text("panes.look_for_tools_button");
    const std::string reason = rv_editor_text("panes.tools_not_while_running");
    const auto trouble = [&f] {
        f.app().project.manifest_error = "disc.toml:3: broken";
    };
    f.states[1]();
    trouble();
    const auto idle = f.settled(roomy_w, roomy_h);
    const auto *idle_button = button_of(idle, label);
    RV_EDITOR_CHECK(idle_button != nullptr);
    RV_EDITOR_CHECK(idle_button == nullptr || !says_on_hover(f, 1, label, reason, trouble));
    f.states[5]();
    trouble();
    const auto busy = f.settled(roomy_w, roomy_h);
    const auto *busy_button = button_of(busy, label);
    RV_EDITOR_CHECK(busy_button != nullptr);
    RV_EDITOR_CHECK(idle_button != nullptr && busy_button != nullptr && same_rect_of(*idle_button, *busy_button));
    RV_EDITOR_CHECK(says_on_hover(f, 5, label, reason, trouble));
    RV_EDITOR_CHECK(f.app().build.busy());
}

// Assets: Stop of a sound is disabled while nothing plays, with the reason; Play is not.
RV_EDITOR_TEST(rv_editor_test_s15_assets_stop_is_disabled_while_nothing_plays)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::assets);
    rv_editor_sound_stop();
    const std::string reason = rv_editor_text("asset_preview.tooltip_nothing_playing");
    constexpr size_t sound_state = 6;
    RV_EDITOR_CHECK(says_on_hover(f, sound_state, rv_editor_text("asset_preview.button_stop"), reason));
    RV_EDITOR_CHECK(!says_on_hover(f, sound_state, rv_editor_text("asset_preview.button_play"), reason));
}

// Files: while the request of a question waits, its Ok and Cancel are disabled with the reason, and the question stays.
RV_EDITOR_TEST(rv_editor_test_s15_files_question_buttons_are_disabled_while_the_request_waits)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::files);
    constexpr size_t new_file_state = 2;
    const std::string reason = rv_editor_text("app_why_not.files_pending");
    const std::string ok = rv_editor_text("pane_files.ok_button");
    const std::string cancel = rv_editor_text("pane_files.cancel_button");
    f.states[new_file_state]();
    const auto before = f.settled(roomy_w, roomy_h);
    const auto *ok_before = button_of(before, ok);
    const auto *cancel_before = button_of(before, cancel);
    RV_EDITOR_CHECK(ok_before != nullptr && cancel_before != nullptr);
    RV_EDITOR_CHECK(ok_before == nullptr || !says_on_hover(f, new_file_state, ok, reason));
    RV_EDITOR_CHECK(cancel_before == nullptr || !says_on_hover(f, new_file_state, cancel, reason));
    f.states[new_file_state]();
    (void)rv_editor_app_request(f.app(), { rv_editor_pane_request_kind::create_file, pane, f.where, "new.txt" });
    RV_EDITOR_CHECK(rv_editor_app_request_pending(f.app(), rv_editor_pane_request_kind::create_file));
    const auto waiting = f.settled(roomy_w, roomy_h);
    const auto *ok_wait = button_of(waiting, ok);
    const auto *cancel_wait = button_of(waiting, cancel);
    RV_EDITOR_CHECK(ok_wait != nullptr && cancel_wait != nullptr);
    if (ok_before == nullptr || cancel_before == nullptr || ok_wait == nullptr || cancel_wait == nullptr) {
        return;
    }
    RV_EDITOR_CHECK(same_rect_of(*ok_before, *ok_wait) && same_rect_of(*cancel_before, *cancel_wait));
    const auto cancel_entry = *cancel_wait;
    const auto ok_entry = *ok_wait;
    const auto draw = [&] {
        f.draw(roomy_w, roomy_h);
    };
    hover(ok_entry, draw);
    RV_EDITOR_CHECK(tooltip_says(gui, reason));
    away(gui);
    hover(cancel_entry, draw);
    RV_EDITOR_CHECK(tooltip_says(gui, reason));
    away(gui);
    press(cancel_entry, draw);
    RV_EDITOR_CHECK(f.view().dialog == dialog::new_file);
}
