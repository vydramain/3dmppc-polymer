// S15, R12/R19/R20, AC13: Files keeps its tree in a content area right under a shelf, with no message block; the short
// questions (new file, new folder, rename, delete a file) are asked in the strip under the tree, the first two and the
// rename with a name field in the strip's button row; the delete of a folder, which lists what goes, replaces the tile's
// content with the mode card and gives it back as it was; the row menu of the tree works.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_pane_tiled.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"
#include "rv_editor_test_strip.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s15;
using dialog = rv_editor_files_view::rv_editor_files_dialog;

namespace
{

constexpr size_t tree_state = 1;
constexpr size_t new_file_state = 2;
constexpr size_t rename_state = 3;
constexpr size_t delete_file_state = 4;
constexpr size_t delete_folder_state = 5;
constexpr float roomy_w = 700.0f;
constexpr float roomy_h = 420.0f;

void report(const char *what, const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  files %s: %s\n", what, bad.c_str());
    }
}

// The scroll window of the (first) content area, and its ImGui window.
ImGuiWindow *tree_scroll(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        if (is_area_scroll(e)) {
            return ImGui::FindWindowByName(e.label.c_str());
        }
    }
    return nullptr;
}

} // namespace

// R19, R20: the shelf and the tree's area follow each other with nothing between them (the old ask place was 23 lines of
// nothing, the box after it two lines and a button row): the area starts where the shelf ends, at the minimum and roomy.
RV_EDITOR_TEST(rv_editor_test_s15_files_tree_is_in_an_area_right_under_a_shelf)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::files);
    float shelf_block = 0.0f;
    float pad = 0.0f;
    float gap = 0.0f;
    gui.frame([&] {
        gap = ImGui::GetStyle().ItemSpacing.y;
        shelf_block = rv_editor_tiled_shelf_block();
        pad = ImGui::GetStyle().WindowPadding.y;
    });
    for (const ImVec2 size : tile_sizes(gui, f.kind, ImVec2(roomy_w, roomy_h))) {
        f.states[tree_state]();
        const auto r = f.settled(size.x, size.y);
        const auto *area = content_area(r);
        RV_EDITOR_CHECK(area != nullptr);
        if (area == nullptr) {
            continue;
        }
        RV_EDITOR_CHECK(!strip::has_message_window(r));
        for (const char *shelf_item : { "##new_file", "##new_dir", "##rename", "##delete", "##refresh" }) {
            const auto *e = find_id(r, shelf_item);
            RV_EDITOR_CHECK(e != nullptr && e->y1 <= area->y0);
        }
        const float reserve = area->y0 - (pad + shelf_block) - gap;
        if (reserve > 1.0f) {
            std::fprintf(stderr, "  files at %.0fx%.0f: %.1f px of nothing above the tree\n", size.x, size.y, reserve);
        }
        RV_EDITOR_CHECK(reserve <= 1.0f);
        RV_EDITOR_CHECK(items_in(r, *area) >= 3); // the root, a file, a folder
    }
}

// R11, R2: new file, rename and delete a file are asked in the strip under the tree: the question's buttons in the strip's
// row, the name field left of the buttons for the first two; the shelf and the tree's top and first row stay where they
// were without a question, and the tree gives up its bottom to the strip.
RV_EDITOR_TEST(rv_editor_test_s15_files_short_questions_are_asked_in_the_strip)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::files);
    float char_w = 0.0f;
    float gap = 0.0f;
    float frame_h = 0.0f;
    gui.frame([&] {
        char_w = ImGui::CalcTextSize("0").x;
        gap = ImGui::GetStyle().ItemSpacing.y;
        frame_h = ImGui::GetFrameHeight();
    });
    const std::string ok = rv_editor_text("pane_files.ok_button");
    const std::string del = rv_editor_text("pane_files.delete_button");
    const std::string cancel = rv_editor_text("pane_files.cancel_button");
    for (const ImVec2 size : tile_sizes(gui, f.kind, ImVec2(roomy_w, roomy_h))) {
        f.states[tree_state]();
        const auto idle = f.settled(size.x, size.y);
        for (const size_t state : { new_file_state, rename_state, delete_file_state }) {
            f.states[state]();
            const auto r = f.settled(size.x, size.y);
            const bool named = state != delete_file_state;
            const auto *area = content_area(r);
            const auto *confirm = find_text(r, named ? ok : del);
            const auto *no = find_text(r, cancel);
            const auto *field = find_id(r, "##field");
            RV_EDITOR_CHECK(area != nullptr && confirm != nullptr && no != nullptr);
            RV_EDITOR_CHECK(!strip::has_message_window(r));
            {
                const std::string bad = strip::strip_problems(&idle, r, size, gap, frame_h);
                report("question", bad);
                RV_EDITOR_CHECK(bad.empty());
            }
            if (area == nullptr || confirm == nullptr || no == nullptr) {
                continue;
            }
            RV_EDITOR_CHECK(confirm->y0 >= area->y1 && no->y0 >= area->y1 && confirm->y0 == no->y0);
            RV_EDITOR_CHECK(confirm->x1 <= no->x0 && no->x1 <= size.x);
            if (named) {
                RV_EDITOR_CHECK(field != nullptr);
                if (field != nullptr) {
                    RV_EDITOR_CHECK(field->y0 >= area->y1 && field->y0 < confirm->y1 && field->x1 <= confirm->x0 && field->x0 >= 0.0f);
                    RV_EDITOR_CHECK(field->x1 - field->x0 >= 10.0f * char_w);
                }
            } else {
                RV_EDITOR_CHECK(field == nullptr);
            }
        }
    }
}

// R11: OK sends the request with the name typed, Cancel closes the question, and Enter in the field confirms.
RV_EDITOR_TEST(rv_editor_test_s15_files_box_buttons_answer_the_question)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::files);
    const std::string ok = rv_editor_text("pane_files.ok_button");
    const std::string cancel = rv_editor_text("pane_files.cancel_button");
    f.states[new_file_state]();
    std::snprintf(f.view().name, sizeof(f.view().name), "%s", "fresh.txt");
    auto r = f.settled(roomy_w, roomy_h);
    const auto *button = find_text(r, ok);
    RV_EDITOR_CHECK(button != nullptr && content_area(r) != nullptr && button->y0 >= content_area(r)->y1);
    if (button != nullptr) {
        f.app().requests.clear();
        f.click(*button, roomy_w, roomy_h);
        RV_EDITOR_CHECK(f.app().requests.size() == 1);
        if (!f.app().requests.empty()) {
            const auto &q = f.app().requests.front();
            RV_EDITOR_CHECK(q.kind == rv_editor_pane_request_kind::create_file && q.name == "fresh.txt" && q.path == f.where);
        }
    }
    f.states[rename_state]();
    r = f.settled(roomy_w, roomy_h);
    const auto *no = find_text(r, cancel);
    RV_EDITOR_CHECK(no != nullptr && content_area(r) != nullptr && no->y0 >= content_area(r)->y1);
    if (no != nullptr) {
        f.click(*no, roomy_w, roomy_h);
        RV_EDITOR_CHECK(f.view().dialog == dialog::none);
        const auto closed = f.settled(roomy_w, roomy_h);
        RV_EDITOR_CHECK(find_text(closed, cancel) == nullptr && !strip::has_message_window(closed)); // the strip is gone
    }
    // Enter in the name field of a question just asked: the field has the focus, a letter, Enter.
    f.states[new_file_state]();
    f.view().opening = true;
    f.app().requests.clear(); // a request still waiting would disable the field
    f.settled(roomy_w, roomy_h);
    ImGuiIO &io = ImGui::GetIO();
    io.AddInputCharacter('x');
    f.draw(roomy_w, roomy_h);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    f.draw(roomy_w, roomy_h);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    f.draw(roomy_w, roomy_h);
    RV_EDITOR_CHECK(!f.app().requests.empty());
    if (!f.app().requests.empty()) {
        RV_EDITOR_CHECK(f.app().requests.front().name == "x");
    }
}

// R12, AC13: deleting a folder lists what goes in the mode card, which takes the tile's content in place of the tree
// (the area is gone, its text area is larger than eight lines of text); the answer gives the tree back in
// the same rectangles at the same scroll.
RV_EDITOR_TEST(rv_editor_test_s15_files_delete_folder_is_a_mode_card_and_gives_the_tree_back)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::files);
    const std::string del = rv_editor_text("pane_files.delete_button");
    const std::string cancel = rv_editor_text("pane_files.cancel_button");
    f.states[tree_state]();
    auto before = f.settled(roomy_w, roomy_h);
    ImGuiWindow *scroll = tree_scroll(before);
    RV_EDITOR_CHECK(scroll != nullptr && scroll->ScrollMax.y > 0.0f); // the tree has rows to scroll
    if (scroll == nullptr) {
        return;
    }
    scroll->Scroll.y = scroll->ScrollMax.y / 2.0f;
    before = f.settled(roomy_w, roomy_h);
    const float scrolled = tree_scroll(before) != nullptr ? tree_scroll(before)->Scroll.y : -1.0f;
    RV_EDITOR_CHECK(scrolled > 0.0f);
    float line = 0.0f;
    gui.frame([&] {
        line = ImGui::GetTextLineHeight();
    });

    f.states[delete_folder_state]();
    const auto card = f.settled(roomy_w, roomy_h);
    RV_EDITOR_CHECK(content_area(card) == nullptr); // the tree is replaced, not covered
    const auto *text = message_box(card);           // the card's text area, the one scroll window left
    RV_EDITOR_CHECK(text != nullptr && text->y1 - text->y0 > 8.0f * line);
    const auto *yes = find_text(card, del);
    const auto *no = find_text(card, cancel);
    RV_EDITOR_CHECK(yes != nullptr && no != nullptr);
    RV_EDITOR_CHECK(yes != nullptr && no != nullptr && yes->y0 == no->y0 && yes->x1 <= no->x0 && no->x1 <= roomy_w);
    // The list of what goes is in the card's text.
    const auto counts = glyph_counts(f, roomy_w, roomy_h, "Q");
    RV_EDITOR_CHECK(counts[0] > 0);
    if (no == nullptr) {
        return;
    }
    f.click(*no, roomy_w, roomy_h);
    RV_EDITOR_CHECK(f.view().dialog == dialog::none);
    const auto after = f.settled(roomy_w, roomy_h);
    report("after the answer", windows_difference(before, after));
    RV_EDITOR_CHECK(windows_difference(before, after).empty());
    RV_EDITOR_CHECK(content_area(after) != nullptr);
    const ImGuiWindow *again = tree_scroll(after);
    RV_EDITOR_CHECK(again != nullptr && again->Scroll.y == scrolled);
}

// R26: a right click on a tree row opens the row menu, and its Rename asks in the strip.
RV_EDITOR_TEST(rv_editor_test_s15_files_row_menu_works_in_the_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::files);
    f.states[tree_state]();
    auto r = f.settled(roomy_w, roomy_h);
    const auto *area = content_area(r);
    RV_EDITOR_CHECK(area != nullptr);
    if (area == nullptr) {
        return;
    }
    const rv_editor_probe_entry *row = nullptr;
    for (const auto &e : r.entries) {
        row = row == nullptr && e.label.find("a.txt") != std::string::npos && inside(e, *area) ? &e : row;
    }
    RV_EDITOR_CHECK(row != nullptr);
    if (row == nullptr) {
        return;
    }
    const std::string rename = rv_editor_text("pane_files.menu_rename");
    RV_EDITOR_CHECK(find_text(r, rename) == nullptr); // closed until the click
    f.click(*row, roomy_w, roomy_h, 1);
    r = f.settled(roomy_w, roomy_h);
    for (const char *key :
        { "pane_files.menu_new_file", "pane_files.menu_new_folder", "pane_files.menu_rename", "pane_files.menu_delete" }) {
        RV_EDITOR_CHECK(find_text(r, rv_editor_text(key)) != nullptr);
    }
    const auto *item = find_text(r, rename);
    if (item == nullptr) {
        return;
    }
    f.click(*item, roomy_w, roomy_h);
    RV_EDITOR_CHECK(f.view().dialog == dialog::rename);
    r = f.settled(roomy_w, roomy_h);
    RV_EDITOR_CHECK(find_id(r, "##field") != nullptr && content_area(r) != nullptr && !strip::has_message_window(r));
}
