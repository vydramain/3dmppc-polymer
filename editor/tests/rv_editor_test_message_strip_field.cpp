// The message strip's name field: it stands in the strip's row left of the buttons, inside the strip, and Enter in it
// presses button 0 only while that button is active (a disabled button 0 is never returned, as in the old box).

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
constexpr float width = 520.0f;
constexpr float height = 120.0f;
const std::vector<std::string> ok_cancel = { "OK", "Cancel" };

struct drawn {
    int pressed = -1;
    rv_editor_probe_record record;
};

drawn draw_strip(rv_editor_test_imgui &gui, char *buf, bool focus, bool ok_disabled)
{
    rv_editor_message_strip_field field;
    field.buf = buf;
    field.size = buf_size;
    field.focus = focus;
    std::vector<rv_editor_state> states(2);
    states[0].disabled = ok_disabled ? "name is empty" : nullptr;
    drawn d;
    d.record = rv_editor_probe_draw(
        gui,
        width,
        [&](rv_editor_probe_record &) {
            d.pressed = rv_editor_message_strip("ask",
                "New file in the project",
                rv_editor_label_kind::normal,
                ok_cancel,
                theme,
                0.0f,
                &states,
                &field);
        },
        height);
    return d;
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label.substr(0, e.label.find("##")) == label ||
            (label[0] == '#' && e.label.compare(0, label.size(), label) == 0)) {
            return &e;
        }
    }
    return nullptr;
}

// Types "x" in the field and presses Enter; returns what the strip answered over all the frames.
int type_and_enter(bool ok_disabled, std::string &typed)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui(true);
    std::vector<char> buf(buf_size, '\0');
    draw_strip(gui, buf.data(), true, ok_disabled); // the field takes the focus
    draw_strip(gui, buf.data(), false, ok_disabled);
    ImGuiIO &io = ImGui::GetIO();
    io.AddInputCharacter('x');
    draw_strip(gui, buf.data(), false, ok_disabled);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    int pressed = draw_strip(gui, buf.data(), false, ok_disabled).pressed;
    io.AddKeyEvent(ImGuiKey_Enter, false);
    pressed = std::max(pressed, draw_strip(gui, buf.data(), false, ok_disabled).pressed);
    typed = buf.data();
    return pressed;
}

} // namespace

// The field is in the strip: its row is the buttons' row, it ends left of the first button and lies inside the strip.
RV_EDITOR_TEST(rv_editor_test_message_strip_field_sits_in_the_row_left_of_the_buttons)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui(true);
    std::vector<char> buf(buf_size, '\0');
    draw_strip(gui, buf.data(), false, false);
    const auto r = draw_strip(gui, buf.data(), false, false).record;
    const auto *field = find(r, "##field");
    const auto *ok = find(r, "OK");
    const auto *cancel = find(r, "Cancel");
    RV_EDITOR_CHECK(field != nullptr && ok != nullptr && cancel != nullptr);
    if (field == nullptr || ok == nullptr) {
        return;
    }
    RV_EDITOR_CHECK(field->x1 <= ok->x0 && field->y0 < ok->y1 && field->y1 > ok->y0); // one row, left of the buttons
    RV_EDITOR_CHECK(field->x1 - field->x0 > 4.0f * ImGui::CalcTextSize("0").x);
    // The strip's frame (the widest item) holds the field and the buttons.
    const rv_editor_probe_entry *frame = nullptr;
    for (const auto &e : r.entries) {
        frame =
            frame == nullptr || (e.x1 - e.x0) * (e.y1 - e.y0) > (frame->x1 - frame->x0) * (frame->y1 - frame->y0) ? &e : frame;
    }
    RV_EDITOR_CHECK(frame != nullptr && field->x0 >= frame->x0 - 0.5f && field->y0 >= frame->y0 - 0.5f &&
        field->y1 <= frame->y1 + 0.5f && field->x1 <= frame->x1 + 0.5f);
}

RV_EDITOR_TEST(rv_editor_test_message_strip_enter_in_the_field_does_not_press_a_disabled_button_0)
{
    std::string typed;
    const int pressed = type_and_enter(true, typed);
    RV_EDITOR_CHECK(typed == "x"); // the key reached the field
    RV_EDITOR_CHECK(pressed == -1);
}

// Control: with button 0 active the same keys press it.
RV_EDITOR_TEST(rv_editor_test_message_strip_enter_in_the_field_presses_an_active_button_0)
{
    std::string typed;
    const int pressed = type_and_enter(false, typed);
    RV_EDITOR_CHECK(typed == "x");
    RV_EDITOR_CHECK(pressed == 0);
}
