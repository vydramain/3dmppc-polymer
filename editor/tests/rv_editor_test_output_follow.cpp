// Console Output in follow mode: with lines appended every frame the lines child sits at its bottom in every frame,
// and a wheel scroll up stops following.

#include <cmath>
#include <string>
#include <string_view>

#include "app/rv_editor_app.hpp"
#include "font/rv_editor_font.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;

namespace
{

constexpr rv_editor_pane_id pane = 1;
constexpr int prefill_lines = 100;
constexpr int warmup_frames = 4;
constexpr int checked_frames = 10;

// The deepest scroll child inside the pane's well: the one that holds the lines.
const ImGuiWindow *lines_child()
{
    const ImGuiWindow *found = nullptr;
    for (const ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        const std::string_view name = w->Name;
        const std::string_view last = name.substr(name.find_last_of('/') + 1);
        if (name.find("##well") != std::string_view::npos && last.find("##scroll") == 0) {
            found = w;
        }
    }
    return found;
}

void add_lines(rv_editor_app &app, int count, int &serial)
{
    for (int i = 0; i < count; ++i) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "line " + std::to_string(serial++));
    }
}

// Appends `per_frame` lines before each of `checked_frames` frames; returns how many ended off the bottom.
int frames_off_bottom(rv_editor_test::rv_editor_test_imgui &gui, int per_frame, bool wrap)
{
    rv_editor_app app;
    const rv_editor_theme theme = rv_editor_theme_olive;
    app.outputs[pane].wrap = wrap;
    app.outputs[pane].follow = true;
    int serial = 0;
    add_lines(app, prefill_lines, serial);
    int off = 0;
    for (int frame = 0; frame < warmup_frames + checked_frames; ++frame) {
        add_lines(app, per_frame, serial);
        gui.frame([&]() {
            rv_editor_pane_output(app, pane, theme);
            const ImGuiWindow *child = lines_child();
            RV_EDITOR_CHECK(child != nullptr);
            if (child != nullptr && frame >= warmup_frames) {
                RV_EDITOR_CHECK(child->ScrollMax.y > 0.0f);
                off += std::fabs(child->Scroll.y - child->ScrollMax.y) > 0.5f ? 1 : 0;
            }
        });
    }
    return off;
}

// A mouse wheel scroll up over the lines turns follow off.
void wheel_up_stops_following(rv_editor_test::rv_editor_test_imgui &gui)
{
    rv_editor_app app;
    const rv_editor_theme theme = rv_editor_theme_olive;
    int serial = 0;
    add_lines(app, prefill_lines, serial);
    for (int frame = 0; frame < warmup_frames; ++frame) {
        gui.frame([&]() {
            rv_editor_pane_output(app, pane, theme);
        });
    }
    RV_EDITOR_CHECK(app.outputs[pane].follow);
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(200.0f, 200.0f);
    gui.frame([&]() {
        rv_editor_pane_output(app, pane, theme);
    });
    io.AddMouseWheelEvent(0.0f, 1.0f);
    gui.frame([&]() {
        rv_editor_pane_output(app, pane, theme);
    });
    RV_EDITOR_CHECK(!app.outputs[pane].follow);
}

} // namespace

// AC1: 1 and 3 lines per frame, wrap on and off: in each of 10 consecutive frames the scroll equals its maximum.
RV_EDITOR_TEST(rv_editor_test_output_follow)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test::rv_editor_test_imgui gui(true);
    RV_EDITOR_CHECK(frames_off_bottom(gui, 1, true) == 0);
    RV_EDITOR_CHECK(frames_off_bottom(gui, 1, false) == 0);
    RV_EDITOR_CHECK(frames_off_bottom(gui, 3, true) == 0);
    RV_EDITOR_CHECK(frames_off_bottom(gui, 3, false) == 0);
    wheel_up_stops_following(gui);
}
