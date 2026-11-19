// S20 (review of S15): Enter in the field of a message box presses button 0 only while that button is active. A
// disabled button 0 is never returned as pressed, by a click or by Enter in the field.

#include <algorithm>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr size_t buf_size = 64;
const std::vector<std::string> ok_cancel = { "OK", "Cancel" };
const char *const why_not = "name is empty";

int draw_box(rv_editor_test_imgui &gui, char *buf, bool focus, bool ok_disabled, float width, float height)
{
    rv_editor_message_box_field field;
    field.buf = buf;
    field.size = buf_size;
    field.focus = focus;
    std::vector<rv_editor_state> states(2);
    states[0].disabled = ok_disabled ? why_not : nullptr;
    int pressed = -1;
    rv_editor_probe_draw(
        gui,
        width,
        [&](rv_editor_probe_record &) {
            pressed = rv_editor_message_box("ask",
                "New file in the project",
                rv_editor_label_kind::normal,
                ok_cancel,
                theme,
                0.0f,
                &field,
                nullptr,
                nullptr,
                &states);
        },
        height);
    return pressed;
}

// Types "x" in the field and presses Enter; returns the index the box answered over all the frames.
int type_and_enter(bool ok_disabled, std::string &typed)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui(true);
    std::vector<char> buf(buf_size, '\0');
    float width = 0.0f;
    float height = 0.0f;
    gui.frame([&] {
        width = rv_editor_message_box_min_width({ ok_cancel }, 20) + 2.0f * ImGui::GetStyle().WindowPadding.x;
        height = rv_editor_message_box_height() + 2.0f * ImGui::GetStyle().WindowPadding.y;
    });
    draw_box(gui, buf.data(), true, ok_disabled, width, height); // the field takes the focus
    draw_box(gui, buf.data(), false, ok_disabled, width, height);
    ImGuiIO &io = ImGui::GetIO();
    io.AddInputCharacter('x');
    draw_box(gui, buf.data(), false, ok_disabled, width, height);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    int pressed = draw_box(gui, buf.data(), false, ok_disabled, width, height);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    pressed = std::max(pressed, draw_box(gui, buf.data(), false, ok_disabled, width, height));
    typed = buf.data();
    return pressed;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_s20_enter_in_the_field_does_not_press_a_disabled_button_0)
{
    std::string typed;
    const int pressed = type_and_enter(true, typed);
    RV_EDITOR_CHECK(typed == "x"); // the key reached the field
    RV_EDITOR_CHECK(pressed == -1);
}

// Control: with button 0 active the same keys press it.
RV_EDITOR_TEST(rv_editor_test_s20_enter_in_the_field_presses_an_active_button_0)
{
    std::string typed;
    const int pressed = type_and_enter(false, typed);
    RV_EDITOR_CHECK(typed == "x");
    RV_EDITOR_CHECK(pressed == 0);
}
