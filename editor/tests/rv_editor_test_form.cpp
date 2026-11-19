// The form widget: the label column follows the longest label and never overlaps a field; values and hints are shown
// whole and the minimum follows the longest of them; a can_fail row keeps its error line, and the button row its
// place, with and without an error; the whole form fits its minimum.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_form.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr size_t buf_size = 256;
const std::string long_label = "A_label_of_exactly_thirty_chr"; // 29 + 1 below
const std::string error_text = "No file at build/3dmpc";

struct form_state {
    std::vector<std::string> values; // by row
    const char *error = nullptr;     // under the can_fail row
};

std::string label30()
{
    return long_label + "x";
}

struct spec {
    std::vector<std::string> labels;
    std::vector<std::string> hints;
    std::vector<bool> fail;
};

// Name (short), the 30 character label, Runtime (can fail), Working dir.
spec default_spec()
{
    return { { "Name", label30(), "Runtime", "Working dir" },
        { "Profile name", "a hint", "empty: the editor's own console", "empty: the project directory" },
        { false, false, true, false } };
}

std::vector<rv_editor_form_row_desc> descs(const spec &s, const form_state &st)
{
    std::vector<rv_editor_form_row_desc> rows;
    for (size_t i = 0; i < s.labels.size(); ++i) {
        rows.push_back({ s.labels[i].c_str(), st.values[i].c_str(), s.hints[i].c_str(), s.fail[i] });
    }
    return rows;
}

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

float text_w(const std::string &s)
{
    return ImGui::CalcTextSize(s.c_str()).x;
}

// The form as a pane would draw it.
void draw_form(const spec &s, const form_state &st)
{
    const auto rows = descs(s, st);
    rv_editor_form_begin(rows, theme);
    for (size_t i = 0; i < s.labels.size(); ++i) {
        std::vector<char> buf(buf_size, '\0');
        std::snprintf(buf.data(), buf.size(), "%s", st.values[i].c_str());
        rv_editor_form_row(s.labels[i].c_str(),
            buf.data(),
            buf.size(),
            s.hints[i].c_str(),
            s.fail[i],
            s.fail[i] ? st.error : nullptr,
            theme);
    }
    rv_editor_form_buttons_begin(theme);
    rv_editor_button("Apply", theme);
    ImGui::SameLine();
    rv_editor_button("Revert", theme);
    rv_editor_form_end();
}

// Window size = the form's minimum plus the window padding on both sides.
ImVec2 min_window(rv_editor_test_imgui &gui, const spec &s, const form_state &st)
{
    ImVec2 size(0, 0);
    gui.frame([&] {
        const ImVec2 pad = ImGui::GetStyle().WindowPadding;
        const auto rows = descs(s, st);
        size = ImVec2(rv_editor_form_min_width(rows) + pad.x * 2.0f, rv_editor_form_min_height(rows) + pad.y * 2.0f);
    });
    return size;
}

rv_editor_probe_record draw_in(rv_editor_test_imgui &gui, const spec &s, const form_state &st, ImVec2 size)
{
    const auto body = [&](rv_editor_probe_record &) {
        draw_form(s, st);
    };
    rv_editor_probe_draw(gui, size.x, body, size.y);
    return rv_editor_probe_draw(gui, size.x, body, size.y);
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label.substr(0, e.label.size()) == label) {
            return &e;
        }
    }
    return nullptr;
}

const rv_editor_probe_entry *field_of(const rv_editor_probe_record &r, const std::string &label)
{
    return find(r, "##" + label);
}

form_state long_values(const spec &s)
{
    form_state st;
    st.values = { "debug-with-a-rather-long-profile-name",
        "src/some/deeply/nested/directory/that/goes/on/and/on/main_script.lua",
        "/home/austyantsev/.local/share/3dmppc/runtime/3dmppc",
        "mppcdiscs/example-cpp-pdkkit" };
    st.values.resize(s.labels.size());
    return st;
}

form_state empty_values(const spec &s)
{
    form_state st;
    st.values.assign(s.labels.size(), "");
    return st;
}

form_state short_values(const spec &s)
{
    form_state st;
    st.values = { "Default", "x", "build/3dmppc", "." };
    st.values.resize(s.labels.size());
    return st;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_form_label_of_30_characters_does_not_cross_its_field)
{
    load_texts();
    rv_editor_test_imgui gui;
    const spec s = default_spec();
    const form_state st = short_values(s);
    const ImVec2 size = min_window(gui, s, st);
    const auto r = draw_in(gui, s, st, size);
    float pad_x = 0, label_w = 0;
    gui.frame([&] {
        pad_x = ImGui::GetStyle().WindowPadding.x;
        label_w = text_w(label30());
    });
    for (const std::string &label : s.labels) {
        const auto *f = field_of(r, label);
        RV_EDITOR_CHECK(f != nullptr);
        if (f != nullptr) {
            // The widest label ends before any field starts.
            RV_EDITOR_CHECK(f->x0 >= pad_x + label_w);
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_form_fields_start_in_one_column_that_follows_the_longest_label)
{
    load_texts();
    rv_editor_test_imgui gui;
    const spec wide = default_spec();
    const spec narrow = { { "Name", "Path" }, { "n", "p" }, { false, false } };
    const form_state wide_st = short_values(wide);
    form_state narrow_st;
    narrow_st.values = { "a", "b" };
    const auto rw = draw_in(gui, wide, wide_st, min_window(gui, wide, wide_st));
    const auto rn = draw_in(gui, narrow, narrow_st, min_window(gui, narrow, narrow_st));
    const auto *first = field_of(rw, wide.labels[0]);
    RV_EDITOR_CHECK(first != nullptr);
    for (const std::string &label : wide.labels) {
        const auto *f = field_of(rw, label);
        RV_EDITOR_CHECK(f != nullptr && first != nullptr && f->x0 == first->x0);
    }
    const auto *short_first = field_of(rn, "Name");
    RV_EDITOR_CHECK(short_first != nullptr && first != nullptr);
    // A form of short labels has a narrower column than one with a 30 character label.
    if (short_first != nullptr && first != nullptr) {
        RV_EDITOR_CHECK(short_first->x0 < first->x0);
    }
}

RV_EDITOR_TEST(rv_editor_test_form_value_and_hint_show_whole_at_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui;
    const spec s = default_spec();
    for (const form_state &st : { long_values(s), empty_values(s) }) {
        const ImVec2 size = min_window(gui, s, st);
        const auto r = draw_in(gui, s, st, size);
        float pad_x = 0;
        std::vector<float> wanted;
        gui.frame([&] {
            pad_x = ImGui::GetStyle().FramePadding.x * 2.0f;
            for (size_t i = 0; i < s.labels.size(); ++i) {
                wanted.push_back(text_w(st.values[i].empty() ? s.hints[i] : st.values[i]) + pad_x);
            }
        });
        for (size_t i = 0; i < s.labels.size(); ++i) {
            const auto *f = field_of(r, s.labels[i]);
            RV_EDITOR_CHECK(f != nullptr);
            if (f != nullptr) {
                RV_EDITOR_CHECK(f->x1 - f->x0 >= wanted[i]);
            }
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_form_minimum_follows_the_longest_value_or_hint)
{
    load_texts();
    rv_editor_test_imgui gui;
    const spec s = default_spec();
    float label_w = 0, longest_value = 0, longest_hint = 0, min_short = 0, min_long = 0, min_empty = 0, pad = 0;
    gui.frame([&] {
        const form_state shorts = short_values(s);
        const form_state longs = long_values(s);
        const form_state empties = empty_values(s);
        min_short = rv_editor_form_min_width(descs(s, shorts));
        min_long = rv_editor_form_min_width(descs(s, longs));
        min_empty = rv_editor_form_min_width(descs(s, empties));
        label_w = text_w(label30());
        for (size_t i = 0; i < s.labels.size(); ++i) {
            longest_value = std::max(longest_value, text_w(longs.values[i]));
            longest_hint = std::max(longest_hint, text_w(s.hints[i]));
        }
        pad = ImGui::GetStyle().FramePadding.x * 2.0f;
    });
    // Label column + the longest text of a field + its frame padding, at least.
    RV_EDITOR_CHECK(min_long >= label_w + longest_value + pad);
    RV_EDITOR_CHECK(min_empty >= label_w + longest_hint + pad);
    RV_EDITOR_CHECK(min_long > min_short);
    RV_EDITOR_CHECK(min_empty >= min_short || longest_hint < longest_value);
}

RV_EDITOR_TEST(rv_editor_test_form_hint_longer_than_every_value_sets_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui;
    const spec plain = { { "Runtime" }, { "x" }, { false } };
    const spec hinted = { { "Runtime" }, { "empty: the editor's own console, which is a long explanation" }, { false } };
    float a = 0, b = 0;
    gui.frame([&] {
        const form_state st = { { "" }, nullptr };
        a = rv_editor_form_min_width(descs(plain, st));
        b = rv_editor_form_min_width(descs(hinted, st));
    });
    RV_EDITOR_CHECK(b > a);
}

RV_EDITOR_TEST(rv_editor_test_form_minimum_height_holds_the_error_line_of_a_can_fail_row_only)
{
    load_texts();
    rv_editor_test_imgui gui;
    float with_fail = 0, without = 0, line = 0;
    gui.frame([&] {
        const form_state st = { { "a", "b" }, nullptr };
        const spec failing = { { "One", "Two" }, { "h", "h" }, { false, true } };
        const spec plain = { { "One", "Two" }, { "h", "h" }, { false, false } };
        with_fail = rv_editor_form_min_height(descs(failing, st));
        without = rv_editor_form_min_height(descs(plain, st));
        line = ImGui::GetTextLineHeight();
    });
    RV_EDITOR_CHECK(with_fail - without >= line);
}

RV_EDITOR_TEST(rv_editor_test_form_error_line_sits_under_its_field_and_the_space_is_always_there)
{
    load_texts();
    rv_editor_test_imgui gui;
    const spec s = default_spec();
    form_state calm = short_values(s);
    form_state failed = calm;
    failed.error = error_text.c_str();
    const ImVec2 size = min_window(gui, s, calm);
    const auto without = draw_in(gui, s, calm, size);
    const auto with = draw_in(gui, s, failed, size);
    float line = 0;
    gui.frame([&] {
        line = ImGui::GetTextLineHeight();
    });
    for (const auto *r : { &without, &with }) {
        const auto *fail = field_of(*r, "Runtime");
        const auto *next = field_of(*r, "Working dir");
        const auto *name = field_of(*r, "Name");
        const auto *after_name = field_of(*r, label30());
        RV_EDITOR_CHECK(fail != nullptr && next != nullptr && name != nullptr && after_name != nullptr);
        if (fail != nullptr && next != nullptr && name != nullptr && after_name != nullptr) {
            // Room for an error line under the can_fail field, none under the others.
            RV_EDITOR_CHECK(next->y0 - fail->y1 >= line);
            RV_EDITOR_CHECK(after_name->y0 - name->y1 < line);
        }
    }
    // The message is drawn under its field: an item that only the failed draw has, between the field and the next row.
    const auto *fail = field_of(with, "Runtime");
    const auto *next = field_of(with, "Working dir");
    bool message = false;
    for (const auto &e : with.entries) {
        if (fail != nullptr && next != nullptr && e.y0 >= fail->y1 && e.y1 <= next->y0 && e.label.empty() && e.x1 > e.x0 &&
            e.x0 >= fail->x0 - 1.0f) {
            message = true;
        }
    }
    RV_EDITOR_CHECK(message);
}

RV_EDITOR_TEST(rv_editor_test_form_fields_and_button_row_keep_their_rectangles_with_and_without_an_error)
{
    load_texts();
    rv_editor_test_imgui gui;
    const spec s = default_spec();
    form_state calm = short_values(s);
    form_state failed = calm;
    failed.error = error_text.c_str();
    const ImVec2 size = min_window(gui, s, calm);
    const auto without = draw_in(gui, s, calm, size);
    const auto with = draw_in(gui, s, failed, size);
    for (const std::string &name : { std::string("##Name"),
             "##" + label30(),
             std::string("##Runtime"),
             std::string("##Working dir"),
             std::string("Apply"),
             std::string("Revert") }) {
        const auto *a = find(without, name);
        const auto *b = find(with, name);
        RV_EDITOR_CHECK(a != nullptr && b != nullptr);
        if (a != nullptr && b != nullptr) {
            RV_EDITOR_CHECK(a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1);
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_form_button_row_is_visible_below_the_last_error_line)
{
    load_texts();
    rv_editor_test_imgui gui;
    const spec s = default_spec();
    form_state failed = short_values(s);
    failed.error = error_text.c_str();
    const ImVec2 size = min_window(gui, s, failed);
    const auto r = draw_in(gui, s, failed, size);
    const auto *last = field_of(r, "Working dir");
    for (const char *label : { "Apply", "Revert" }) {
        const auto *b = find(r, label);
        RV_EDITOR_CHECK(b != nullptr);
        if (b != nullptr && last != nullptr) {
            RV_EDITOR_CHECK(b->y0 >= last->y1);
            RV_EDITOR_CHECK(b->y1 <= size.y && b->x1 <= size.x && b->x0 >= 0.0f);
            RV_EDITOR_CHECK(b->text_w <= b->x1 - b->x0);
        }
    }
    const auto *apply = find(r, "Apply");
    const auto *revert = find(r, "Revert");
    RV_EDITOR_CHECK(apply != nullptr && revert != nullptr);
    if (apply != nullptr && revert != nullptr) {
        RV_EDITOR_CHECK(revert->x0 > apply->x1 - 1.0f); // a row: Apply, then Revert to its right
    }
}

RV_EDITOR_TEST(rv_editor_test_form_fits_whole_on_its_minimum_in_every_state)
{
    load_texts();
    rv_editor_test_imgui gui;
    const spec s = default_spec();
    // The minimum follows the longest of the values, so the window comes from the long state and serves all of them.
    const form_state longs = long_values(s);
    const ImVec2 size = min_window(gui, s, longs);
    const ImRect tile(0, 0, size.x, size.y);
    form_state failed = longs;
    failed.error = error_text.c_str();
    const std::vector<form_state> states = { short_values(s), longs, failed, empty_values(s) };
    rv_editor_probe_record first;
    for (size_t i = 0; i < states.size(); ++i) {
        const auto r = draw_in(gui, s, states[i], size);
        RV_EDITOR_CHECK(!r.entries.empty());
        RV_EDITOR_CHECK(rv_editor_fits_whole(r, tile).empty());
        if (i == 0) {
            first = r;
        }
        for (const std::string &name : { std::string("##Runtime"), std::string("Apply") }) {
            const auto *a = find(first, name);
            const auto *b = find(r, name);
            RV_EDITOR_CHECK(a != nullptr && b != nullptr);
            if (a != nullptr && b != nullptr) {
                RV_EDITOR_CHECK(a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1);
            }
        }
    }
}
