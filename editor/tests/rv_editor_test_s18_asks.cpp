// S18, R12, AC13: the questions of the Code tile (Unsaved changes, Save As) are cards of the mode-card template, not
// the old ask place; a card can hold a form of its own in its text area (Save As); the card fills the content region the
// code grid had, and once answered the grid has its region back. nvim is not started: the grid is a stand-in.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_s18.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_mode.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s18;

namespace
{

constexpr rv_editor_theme theme = stable_tiles::theme;
constexpr float room_w = 420.0f;
constexpr float room_h = 360.0f;

bool paints(uint32_t rgb)
{
    for (const ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        for (const ImDrawVert &v : w->DrawList->VtxBuffer) {
            if (v.col == rv_editor_col(rgb)) {
                return true;
            }
        }
    }
    return false;
}

// One tile of a code tile as the shell draws it: the asks of pane 1, else the stand-in for the nvim grid that fills the
// content region. `region` is that region, `grid` the stand-in's rectangle (zero while an ask is shown).
struct tile {
    rv_editor_test_imgui &gui;
    rv_editor_shell shell;
    ImVec2 origin{}, avail{};
    bool asked = false;
    rv_editor_probe_entry grid{};

    explicit tile(rv_editor_test_imgui &g)
        : gui(g)
    {
    }

    drawn frame()
    {
        drawn d;
        d.record = rv_editor_probe_draw(
            gui,
            room_w,
            [&](rv_editor_probe_record &p) {
                d.origin = origin = ImGui::GetCursorScreenPos();
                d.avail = avail = ImGui::GetContentRegionAvail();
                asked = rv_editor_shell_code_asks(shell, pane, theme);
                if (!asked) {
                    ImGui::InvisibleButton("##grid", avail);
                    p.item("grid");
                }
            },
            room_h);
        grid = {};
        for (const auto &e : d.record.entries) {
            if (e.label == "grid") {
                grid = e;
            }
        }
        return d;
    }

    drawn settle()
    {
        drawn d;
        for (int i = 0; i < 3; ++i) {
            d = frame();
        }
        return d;
    }

    // A left click on the button named `label` of the ask on screen; true when it was found.
    bool click(const std::string &label)
    {
        const drawn d = settle();
        const rv_editor_probe_entry *b = find(d.record, label);
        if (b == nullptr) {
            return false;
        }
        ImGui::GetIO().AddMousePosEvent((b->x0 + b->x1) / 2.0f, (b->y0 + b->y1) / 2.0f);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        settle();
        ImGui::GetIO().AddMousePosEvent(-1000.0f, -1000.0f);
        frame();
        return true;
    }
};

} // namespace

// The slot: a card with a body draws it inside the text area, once a frame, and the text is not drawn.
RV_EDITOR_TEST(rv_editor_test_s18_a_card_holds_the_body_it_is_given_in_its_text_area)
{
    load_texts();
    rv_editor_test_imgui gui;
    int calls = 0;
    rv_editor_mode_card_spec spec;
    spec.title = "Save As";
    spec.text = "the plain text";
    spec.buttons = { "Save", "Cancel" };
    spec.body = [&] {
        ++calls;
        ImGui::Button("slot_button");
    };
    drawn d;
    for (int i = 0; i < 3; ++i) {
        calls = 0;
        d.record = rv_editor_probe_draw(
            gui,
            room_w,
            [&](rv_editor_probe_record &) {
                (void)rv_editor_mode_card_begin("card", spec, theme);
                rv_editor_mode_card_end();
            },
            room_h);
    }
    RV_EDITOR_CHECK(calls == 1);
    const auto *area = text_area(d.record);
    const auto *slot = find(d.record, "slot_button");
    RV_EDITOR_CHECK(area != nullptr && slot != nullptr);
    if (area != nullptr && slot != nullptr) {
        RV_EDITOR_CHECK(slot->in_scroll);
        RV_EDITOR_CHECK(slot->x0 >= area->x0 && slot->x1 <= area->x1 && slot->y0 >= area->y0 && slot->y1 <= area->y1);
    }
    for (const char *label : { "Save", "Cancel" }) {
        const auto *b = find(d.record, label);
        RV_EDITOR_CHECK(b != nullptr && area != nullptr && b->y0 >= area->y1);
    }
}

// AC13: Unsaved changes fills the region the grid had; Keep Open gives the grid that same region back.
RV_EDITOR_TEST(rv_editor_test_s18_unsaved_ask_is_a_card_over_the_grid_region_and_gives_it_back)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    tile t(gui);
    t.settle();
    const rv_editor_probe_entry before = t.grid;
    RV_EDITOR_CHECK(!t.asked && before.x1 > before.x0);
    const ImVec2 region = t.avail;
    t.shell.closing = pane;
    const drawn asked = t.settle();
    RV_EDITOR_CHECK(t.asked);
    RV_EDITOR_CHECK(asked.avail.x == region.x && asked.avail.y == region.y);
    RV_EDITOR_CHECK(card_of(asked) != nullptr);
    RV_EDITOR_CHECK(!s13::has_ask_place(asked.record));
    for (const std::string &b : unsaved_buttons()) {
        RV_EDITOR_CHECK(find(asked.record, b) != nullptr);
    }
    RV_EDITOR_CHECK(t.click(rv_editor_text("shell_save.keep_open_button")));
    RV_EDITOR_CHECK(t.shell.closing == rv_editor_tile_none);
    t.settle();
    RV_EDITOR_CHECK(!t.asked);
    RV_EDITOR_CHECK(t.grid.x0 == before.x0 && t.grid.y0 == before.y0 && t.grid.x1 == before.x1 && t.grid.y1 == before.y1);
}

// AC13: Save As is a card whose text area holds the path field; Cancel gives the grid its region back.
RV_EDITOR_TEST(rv_editor_test_s18_save_as_is_a_card_with_the_path_field_inside_and_gives_the_grid_back)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    tile t(gui);
    t.settle();
    const rv_editor_probe_entry before = t.grid;
    t.shell.save_as_buffer = 1;
    t.shell.save_as_pane = pane;
    const drawn asked = t.settle();
    RV_EDITOR_CHECK(t.asked);
    RV_EDITOR_CHECK(card_of(asked) != nullptr);
    const auto *area = text_area(asked.record);
    const rv_editor_probe_entry *field = nullptr;
    for (const auto &e : asked.record.entries) {
        field = e.label == "##save_as" ? &e : field;
    }
    RV_EDITOR_CHECK(area != nullptr && field != nullptr);
    if (area != nullptr && field != nullptr) {
        RV_EDITOR_CHECK(field->in_scroll);
        RV_EDITOR_CHECK(field->y0 >= area->y0 && field->y1 <= area->y1);
    }
    for (const std::string &b : save_as_buttons()) {
        const auto *e = find(asked.record, b);
        RV_EDITOR_CHECK(e != nullptr && area != nullptr && e->y0 >= area->y1);
    }
    RV_EDITOR_CHECK(!paints(theme.error));
    t.shell.save_as_error = "it is a directory";
    t.settle();
    RV_EDITOR_CHECK(paints(theme.error));
    RV_EDITOR_CHECK(t.click(rv_editor_text("shell_save.cancel_button")));
    RV_EDITOR_CHECK(t.shell.save_as_buffer == 0);
    t.settle();
    RV_EDITOR_CHECK(!t.asked);
    RV_EDITOR_CHECK(t.grid.x0 == before.x0 && t.grid.y0 == before.y0 && t.grid.x1 == before.x1 && t.grid.y1 == before.y1);
}
