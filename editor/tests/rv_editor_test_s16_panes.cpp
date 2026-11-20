// S16, R11/R13/R19/R26, AC14/AC20: what Hierarchy and Inspector put where. Hierarchy has its shelf, the search and sort
// form, one message box for the target and the move note, and the tree in the content area; its context menu opens from
// a tree row. Inspector keeps its form in the content area under one message box (the read-only banner and the refusal
// of a field), and a label never covers its field while a value is shown whole.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "pdkkit/component/rv_component_record.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s16.hpp"
#include "rv_editor_test_s3.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s16;

namespace
{

// The pane at its minimum, after the state `state` of `f` has been set.
rv_editor_probe_record at_minimum(rv_editor_test_imgui &gui, fx &f, size_t state, stable_tiles::minimum &min)
{
    min = stable_tiles::pane_minimum(gui, f.kind);
    // No minimum yet: the layout is judged in a roomy tile, so what fails is the layout, not the missing minimum.
    min = min.w > 0.0f && min.h > 0.0f ? min : stable_tiles::minimum{ 600.0f, 500.0f };
    f.states[state]();
    return settled(f, min.w, min.h);
}

bool center_inside(const rv_editor_probe_entry &e, const rv_editor_probe_entry &area)
{
    const float cx = (e.x0 + e.x1) / 2.0f;
    const float cy = (e.y0 + e.y1) / 2.0f;
    return cx >= area.x0 && cx <= area.x1 && cy >= area.y0 && cy <= area.y1;
}

// The first item with a label inside the area (a tree row), nullptr when there is none.
const rv_editor_probe_entry *first_row(const rv_editor_probe_record &r, const rv_editor_probe_entry *area)
{
    for (const auto &e : r.entries) {
        if (area != nullptr && !e.label.empty() && !e.scroll_area && center_inside(e, *area)) {
            return &e;
        }
    }
    return nullptr;
}

int count_boxes(const rv_editor_probe_record &r)
{
    return s3::boxes(r);
}

} // namespace

// R19, AC20, R2: Hierarchy is a shelf, a form of the search field and the sort choice, the tree in the content area and a
// message strip under it only for a real message (the refusal of a move), in that order from the top, no message box, and the
// old lines of the target and the note are gone.
RV_EDITOR_TEST(rv_editor_test_s16_hierarchy_is_a_shelf_a_form_a_tree_in_an_area_and_a_strip)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fx f(gui, rv_editor_pane_kind::hierarchy);
    stable_tiles::minimum min{ 0, 0 };
    // 1 tree, 4 move note, 5 selection: the target and the note say themselves in the one box.
    for (const size_t state : { size_t(1), size_t(4), size_t(5) }) {
        const auto r = at_minimum(gui, f, state, min);
        const auto *area = content_area(r);
        const auto *strip = s3::strip_of(r);
        const auto *query = find(r, "##hierarchy_query");
        const auto *sort = find(r, "##hierarchy_sort");
        const auto *add = find(r, "##add");
        // The target is a placeholder, not a message: only the move note (4) is a strip.
        RV_EDITOR_CHECK((strip != nullptr) == (state == 4));
        RV_EDITOR_CHECK(area != nullptr && query != nullptr && sort != nullptr && add != nullptr);
        if (area == nullptr || query == nullptr || sort == nullptr || add == nullptr) {
            std::fprintf(stderr, "  state %zu: the area, search, sort or shelf is missing\n", state);
            continue;
        }
        RV_EDITOR_CHECK(count_boxes(r) == 0);
        RV_EDITOR_CHECK(find(r, "##target") == nullptr && find(r, "##note") == nullptr);
        RV_EDITOR_CHECK(!has_ask_place(r));
        // The shelf and the form are above the area, the strip under it; the tree's rows are inside the area.
        RV_EDITOR_CHECK(add->y1 <= area->y0 && query->y1 <= area->y0 && sort->y1 <= area->y0 &&
            (strip == nullptr || strip->y0 >= area->y1 - 0.5f));
        RV_EDITOR_CHECK(first_row(r, area) != nullptr);
        // The search field and the sort choice are the form: whole, inside the tile, the field before the choice.
        RV_EDITOR_CHECK(query->x1 <= sort->x0 + 0.5f || query->y1 <= sort->y0 + 0.5f);
        RV_EDITOR_CHECK(sort->x1 <= min.w && query->x1 <= min.w);
    }
}

// R26: a right click on a tree row, which lives in the content area, opens the context menu with its commands.
RV_EDITOR_TEST(rv_editor_test_s16_hierarchy_context_menu_opens_from_a_row_in_the_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fx f(gui, rv_editor_pane_kind::hierarchy);
    stable_tiles::minimum min{ 0, 0 };
    const auto r = at_minimum(gui, f, 1, min);
    const auto *row = first_row(r, content_area(r));
    RV_EDITOR_CHECK(row != nullptr);
    if (row == nullptr) {
        std::fprintf(stderr, "  no tree row inside a content area\n");
        return;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((row->x0 + row->x1) / 2.0f, (row->y0 + row->y1) / 2.0f);
    f.draw(min.w, min.h);
    io.AddMouseButtonEvent(1, true);
    f.draw(min.w, min.h);
    io.AddMouseButtonEvent(1, false);
    f.draw(min.w, min.h);
    const auto menu = rv_editor_probe_draw(
        gui,
        min.w,
        [&](rv_editor_probe_record &) {
            rv_editor_shell_pane(&f.shell, pane, f.kind, stable_tiles::theme);
        },
        min.h);
    io.AddMousePosEvent(-1000.0f, -1000.0f);
    RV_EDITOR_CHECK(find(menu, rv_editor_text("pane_hierarchy.duplicate")) != nullptr);
    RV_EDITOR_CHECK(find(menu, rv_editor_text("pane_hierarchy.delete")) != nullptr);
}

// R13, R19, R2: Inspector keeps its form in the content area and its banner and refusal in one strip under it (none with
// nothing to say), with no message box and no message line of its own.
RV_EDITOR_TEST(rv_editor_test_s16_inspector_form_is_in_the_area_over_one_strip)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fx f(gui, rv_editor_pane_kind::inspector);
    stable_tiles::minimum min{ 0, 0 };
    // 2 object, 3 read-only, 4 refusal.
    for (const size_t state : { size_t(2), size_t(3), size_t(4) }) {
        const auto r = at_minimum(gui, f, state, min);
        const auto *area = content_area(r);
        const auto *strip = s3::strip_of(r);
        const auto *name = find(r, "##Name");
        RV_EDITOR_CHECK(area != nullptr && name != nullptr);
        if (area == nullptr || name == nullptr) {
            std::fprintf(stderr, "  state %zu: the area or Name field is missing\n", state);
            continue;
        }
        RV_EDITOR_CHECK((strip != nullptr) == (state != 2)); // nothing to say in state 2
        RV_EDITOR_CHECK(count_boxes(r) == 0);
        RV_EDITOR_CHECK(find(r, "##read_only") == nullptr);
        RV_EDITOR_CHECK(!has_ask_place(r));
        RV_EDITOR_CHECK(strip == nullptr || strip->y0 >= area->y1 - 0.5f);
        RV_EDITOR_CHECK(center_inside(*name, *area));
    }
}

// AC14: at the minimum a label ends before its field starts (or is above it), and a value of the Name field and of a
// component's field is shown whole: the field is as wide as its text.
RV_EDITOR_TEST(rv_editor_test_s16_inspector_label_clears_its_field_and_the_value_is_whole)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fx f(gui, rv_editor_pane_kind::inspector);
    stable_tiles::minimum min{ 0, 0 };
    f.states[2]();
    const std::string long_name = words(100);
    rv_editor_object_set_name(f.object(), long_name);
    rv_pdklib::rv_manifest_tree_entry e;
    e.key = long_label;
    const std::string long_value = words(120);
    e.value.str = long_value;
    f.object().components.back().fields.push_back(e);
    min = stable_tiles::pane_minimum(gui, f.kind);
    min = min.w > 0.0f && min.h > 0.0f ? min : stable_tiles::minimum{ 600.0f, 500.0f };
    // Minimum width, roomy height: the area keeps only a few rows at the minimum (R4), and a field scrolled out of it
    // gets no probe entry. The whole-form-at-the-minimum check is s16_fits.
    const auto r = settled(f, min.w, min.h > 900.0f ? min.h : 900.0f);
    const auto *area = content_area(r);
    RV_EDITOR_CHECK(area != nullptr);
    float pad_x = 0.0f;
    float text_name = 0.0f;
    float text_value = 0.0f;
    gui.frame([&] {
        pad_x = ImGui::GetStyle().FramePadding.x;
        text_name = ImGui::CalcTextSize(long_name.c_str()).x;
        text_value = ImGui::CalcTextSize(long_value.c_str()).x;
    });
    const auto *name = find(r, "##Name");
    const auto *value = find(r, "##value");
    RV_EDITOR_CHECK(name != nullptr && value != nullptr);
    if (name != nullptr && value != nullptr) {
        if (name->x1 - name->x0 < text_name + 2.0f * pad_x) {
            std::fprintf(stderr, "  the Name field is %.0f px wide, its value %.0f\n", name->x1 - name->x0, text_name);
        }
        RV_EDITOR_CHECK(name->x1 - name->x0 >= text_name + 2.0f * pad_x);
        if (value->x1 - value->x0 < text_value + 2.0f * pad_x) {
            std::fprintf(stderr, "  the value field is %.0f px wide, its value %.0f\n", value->x1 - value->x0, text_value);
        }
        RV_EDITOR_CHECK(value->x1 - value->x0 >= text_value + 2.0f * pad_x);
    }
    // A text (no label) on the row of a field and left of it does not reach into it.
    for (const auto &t : r.entries) {
        if (!t.label.empty() || t.text_w <= 0.0f || (area != nullptr && !center_inside(t, *area))) {
            continue;
        }
        for (const auto &field : r.entries) {
            const bool is_field = field.label.compare(0, 2, "##") == 0 && !field.scroll_area;
            const bool same_row = field.y0 < t.y1 && field.y1 > t.y0 && field.x0 > t.x0;
            if (is_field && same_row && t.x0 + t.text_w > field.x0 + 0.5f) {
                std::fprintf(stderr,
                    "  a label of %.0f px at %.0f reaches into the field %s at %.0f\n",
                    t.text_w,
                    t.x0,
                    field.label.c_str(),
                    field.x0);
                RV_EDITOR_CHECK(false);
                return;
            }
        }
    }
}
