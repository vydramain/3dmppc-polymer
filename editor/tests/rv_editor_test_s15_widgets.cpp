// S15, R11, R13: the two widget variants the panes of S15 need. The message box takes an input field in its button row
// (Files asks for a name there); the form takes a row whose field is the caller's (a path field, a text area, a check
// box) in the label column, with the error line reserved and the minimum counting the field's value.

#include <cstdio>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_form.hpp"
#include "ui/rv_editor_widgets_message.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr int field_chars = 20;
constexpr size_t buf_size = 256;

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label.compare(0, label.size(), label) == 0 && e.label.find('/') == std::string::npos) {
            return &e;
        }
    }
    return nullptr;
}

const rv_editor_probe_entry *text_window(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        if (e.scroll_area) {
            return &e;
        }
    }
    return nullptr;
}

// --- the message box with a field

const std::vector<std::string> ok_cancel = { "OK", "Cancel" };

struct box_frame {
    rv_editor_probe_record record;
    int pressed = -1;
};

// `height` < 0: measured in a frame of its own first, which takes the input of the frame that follows; pass it to keep
// typing and keys for the frame with the field.
box_frame draw_box(rv_editor_test_imgui &gui, char *buf, bool with_field, bool focus, float width, float height = -1.0f)
{
    box_frame out;
    rv_editor_message_box_field field;
    field.buf = buf;
    field.size = buf_size;
    field.hint = "name";
    field.focus = focus;
    if (height < 0.0f) {
        gui.frame([&] {
            height = rv_editor_message_box_height() + 2.0f * ImGui::GetStyle().WindowPadding.y;
        });
    }
    out.record = rv_editor_probe_draw(
        gui,
        width,
        [&](rv_editor_probe_record &) {
            out.pressed = rv_editor_message_box("ask",
                "New file in the project",
                rv_editor_label_kind::normal,
                ok_cancel,
                theme,
                0.0f,
                with_field ? &field : nullptr);
        },
        height);
    return out;
}

float min_box_window(rv_editor_test_imgui &gui)
{
    float w = 0.0f;
    gui.frame([&] {
        w = rv_editor_message_box_min_width({ ok_cancel }, field_chars) + 2.0f * ImGui::GetStyle().WindowPadding.x;
    });
    return w;
}

} // namespace

// R11: at the box's minimum width the field sits in the button row, right of the text area's bottom edge and left of
// the buttons, at least `field_chars` characters wide, and the box is as high as without a field.
RV_EDITOR_TEST(rv_editor_test_s15_message_box_field_sits_in_the_button_row)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    std::vector<char> buf(buf_size, '\0');
    const float width = min_box_window(gui);
    draw_box(gui, buf.data(), true, false, width);
    const auto with = draw_box(gui, buf.data(), true, false, width);
    draw_box(gui, buf.data(), false, false, width);
    const auto without = draw_box(gui, buf.data(), false, false, width);
    float char_w = 0.0f;
    gui.frame([&] {
        char_w = ImGui::CalcTextSize("0").x;
    });
    const auto *field = find(with.record, "##field");
    const auto *text = text_window(with.record);
    const auto *ok = find(with.record, "OK");
    const auto *cancel = find(with.record, "Cancel");
    RV_EDITOR_CHECK(field != nullptr && text != nullptr && ok != nullptr && cancel != nullptr);
    if (field != nullptr && text != nullptr && ok != nullptr && cancel != nullptr) {
        RV_EDITOR_CHECK(field->y0 >= text->y1);
        RV_EDITOR_CHECK(field->x1 <= ok->x0 && ok->x1 <= cancel->x0);
        RV_EDITOR_CHECK(field->x1 - field->x0 >= static_cast<float>(field_chars) * char_w);
        RV_EDITOR_CHECK(field->y0 == ok->y0);
    }
    RV_EDITOR_CHECK(with.record.content_height == without.record.content_height);
    RV_EDITOR_CHECK(find(without.record, "##field") == nullptr);
    const auto *plain_text = text_window(without.record);
    RV_EDITOR_CHECK(text != nullptr && plain_text != nullptr && text->x0 == plain_text->x0 && text->y0 == plain_text->y0 &&
        text->x1 == plain_text->x1 && text->y1 == plain_text->y1);
}

// R11: Enter in the field presses the first button, and what was typed is in the buffer.
RV_EDITOR_TEST(rv_editor_test_s15_message_box_enter_in_the_field_presses_the_first_button)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    std::vector<char> buf(buf_size, '\0');
    const float width = min_box_window(gui);
    float height = 0.0f;
    gui.frame([&] {
        height = rv_editor_message_box_height() + 2.0f * ImGui::GetStyle().WindowPadding.y;
    });
    draw_box(gui, buf.data(), true, true, width, height); // the question was just asked: the field takes the focus
    draw_box(gui, buf.data(), true, false, width, height);
    ImGuiIO &io = ImGui::GetIO();
    io.AddInputCharacter('x');
    draw_box(gui, buf.data(), true, false, width, height);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    int pressed = draw_box(gui, buf.data(), true, false, width, height).pressed;
    io.AddKeyEvent(ImGuiKey_Enter, false);
    pressed = std::max(pressed, draw_box(gui, buf.data(), true, false, width, height).pressed);
    RV_EDITOR_CHECK(std::string(buf.data()) == "x");
    RV_EDITOR_CHECK(pressed == 0);
}

// --- the form with a custom row

namespace
{

constexpr size_t custom_buf = 512;

struct form_world {
    std::string root = "/project/root";
    std::string path = "/project/root/src/some/deeply/nested/directory/that/goes/on/and/on/main_script_with_a_long_name.lua";
    std::string rel = "src/some/deeply/nested/directory/that/goes/on/and/on/main_script_with_a_long_name.lua";
    const char *error = nullptr;
    bool flag = false;
};

std::vector<rv_editor_form_row_desc> descs(const form_world &w)
{
    return { { "Name", "Default", "profile name", false },
        { "Runtime", w.rel.c_str(), "empty: the editor's own console", true },
        { "Start Paused", "", nullptr, false },
        { "More options", "", "one per line", false } };
}

void draw_form(form_world &w)
{
    const auto rows = descs(w);
    std::vector<char> name(custom_buf, '\0');
    std::snprintf(name.data(), name.size(), "Default");
    std::vector<char> path(custom_buf, '\0');
    std::snprintf(path.data(), path.size(), "%s", w.path.c_str());
    std::vector<char> more(custom_buf, '\0');
    rv_editor_form_begin(rows, theme);
    rv_editor_form_row("Name", name.data(), name.size(), "profile name", false, nullptr, theme);
    rv_editor_form_custom_begin("Runtime", theme);
    rv_editor_path_field("##Runtime", path.data(), path.size(), w.root, theme);
    rv_editor_form_custom_end(true, w.error, theme);
    rv_editor_form_custom_begin("Start Paused", theme);
    rv_editor_checkbox("##Paused", &w.flag, theme);
    rv_editor_form_custom_end(false, nullptr, theme);
    rv_editor_form_custom_begin("More options", theme);
    rv_editor_text_area("##More", more.data(), more.size(), 3.0f, theme);
    rv_editor_form_custom_end(false, nullptr, theme);
    rv_editor_form_buttons_begin(theme);
    rv_editor_button("Apply", theme);
    rv_editor_form_end();
}

// The form's own minimum as a window, with the window padding on both sides.
ImVec2 form_window(rv_editor_test_imgui &gui, const form_world &w)
{
    ImVec2 size(0, 0);
    gui.frame([&] {
        const ImVec2 pad = ImGui::GetStyle().WindowPadding;
        const auto rows = descs(w);
        size = ImVec2(rv_editor_form_min_width(rows) + 2.0f * pad.x,
            rv_editor_form_min_height(rows) + 2.0f * pad.y + ImGui::GetFrameHeight());
    });
    return size;
}

rv_editor_probe_record draw_in(rv_editor_test_imgui &gui, form_world &w, ImVec2 size)
{
    const auto body = [&](rv_editor_probe_record &) {
        draw_form(w);
    };
    rv_editor_probe_draw(gui, size.x, body, size.y);
    return rv_editor_probe_draw(gui, size.x, body, size.y);
}

} // namespace

// R13: the fields of the caller's rows start in the column of the text row's field, right of the longest label.
RV_EDITOR_TEST(rv_editor_test_s15_form_custom_rows_share_the_label_column)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    form_world w;
    const auto r = draw_in(gui, w, form_window(gui, w));
    float pad = 0.0f;
    float label_w = 0.0f;
    gui.frame([&] {
        pad = ImGui::GetStyle().WindowPadding.x;
        label_w = ImGui::CalcTextSize("Start Paused").x;
    });
    const auto *name = find(r, "##Name");
    RV_EDITOR_CHECK(name != nullptr);
    for (const char *id : { "##Runtime", "##Paused", "##More" }) {
        const auto *e = find(r, id);
        RV_EDITOR_CHECK(e != nullptr && name != nullptr);
        if (e != nullptr && name != nullptr) {
            RV_EDITOR_CHECK(e->x0 == name->x0);
            RV_EDITOR_CHECK(e->x0 >= pad + label_w);
        }
    }
}

// R13, R2: a path in a custom row shows whole at the form's minimum, from its root, and the minimum counts it.
RV_EDITOR_TEST(rv_editor_test_s15_form_custom_path_shows_whole_at_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    form_world w;
    const ImVec2 size = form_window(gui, w);
    const auto r = draw_in(gui, w, size);
    float text_w = 0.0f;
    float pad = 0.0f;
    gui.frame([&] {
        text_w = ImGui::CalcTextSize(w.rel.c_str()).x;
        pad = ImGui::GetStyle().FramePadding.x * 2.0f;
    });
    const auto *e = find(r, "##Runtime");
    RV_EDITOR_CHECK(e != nullptr);
    const rv_editor_path_probe *p = e != nullptr ? rv_editor_path_probe_of(e->id) : nullptr;
    RV_EDITOR_CHECK(p != nullptr);
    if (e != nullptr && p != nullptr) {
        RV_EDITOR_CHECK(p->shown == w.rel);
        RV_EDITOR_CHECK(e->x1 - e->x0 >= text_w + pad - 0.5f);
        RV_EDITOR_CHECK(e->x1 <= size.x);
    }
}

// R13: the error line of a custom row that can fail is reserved under its field, and the next row and the button row are
// where they are with an error and without one; the others reserve none.
RV_EDITOR_TEST(rv_editor_test_s15_form_custom_error_line_is_reserved_and_the_buttons_stay)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    form_world calm;
    form_world failed;
    failed.error = "No file at src/some/deeply/nested/directory";
    const ImVec2 size = form_window(gui, calm);
    const auto without = draw_in(gui, calm, size);
    const auto with = draw_in(gui, failed, size);
    float line = 0.0f;
    gui.frame([&] {
        line = ImGui::GetTextLineHeight();
    });
    for (const auto *r : { &without, &with }) {
        const auto *runtime = find(*r, "##Runtime");
        const auto *paused = find(*r, "##Paused");
        const auto *more = find(*r, "##More");
        RV_EDITOR_CHECK(runtime != nullptr && paused != nullptr && more != nullptr);
        if (runtime != nullptr && paused != nullptr && more != nullptr) {
            RV_EDITOR_CHECK(paused->y0 - runtime->y1 >= line);
            RV_EDITOR_CHECK(more->y0 - paused->y1 < line);
        }
    }
    const auto *a = find(without, "Apply");
    const auto *b = find(with, "Apply");
    RV_EDITOR_CHECK(a != nullptr && b != nullptr);
    if (a != nullptr && b != nullptr) {
        RV_EDITOR_CHECK(a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1);
    }
    RV_EDITOR_CHECK(without.content_height == with.content_height);
}
