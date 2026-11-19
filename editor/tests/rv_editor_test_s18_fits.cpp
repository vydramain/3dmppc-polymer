// S18, AC1/AC13: the Code tile (without a running nvim: the failed start and the asks of unsaved files) and Review
// Changes at the minimum of their kind show every element whole in every state of the plan's table, the asks fill the
// tile as a card whose text area and buttons keep their places from ask to ask, and one pixel less in width or in
// height breaks at least one state.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_s18.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s18;

namespace
{

// "" when `a` and `b` (two asks of one tile) have the card, the text area and the shared buttons in the same places.
std::string asks_difference(const drawn &a, const drawn &b)
{
    if (card_of(a) == nullptr || card_of(b) == nullptr) {
        return "an ask is not a card over the whole content region";
    }
    const auto *ta = text_area(a.record);
    const auto *tb = text_area(b.record);
    if (ta == nullptr || tb == nullptr) {
        return "an ask has no text area";
    }
    if (ta->x0 != tb->x0 || ta->y0 != tb->y0 || ta->x1 != tb->x1 || ta->y1 != tb->y1) {
        return "the text area " + rv_editor_probe_detail::rect(*ta) + " became " + rv_editor_probe_detail::rect(*tb);
    }
    for (const std::string &label : { rv_editor_text("shell_save.save_button"), rv_editor_text("shell_save.cancel_button") }) {
        const auto *x = find(a.record, label);
        const auto *y = find(b.record, label);
        if (x != nullptr && y != nullptr && (x->y0 != y->y0 || x->y1 != y->y1)) {
            return "button " + label + " moved";
        }
    }
    return "";
}

// "" or the first violation of `kind` at `min` over all its states, on a shell of its own.
std::string run_states(rv_editor_test_imgui &gui, rv_editor_pane_kind kind, stable_tiles::minimum min)
{
    fixture f(gui, kind);
    rv_editor_probe_record plain; // the first state that is not an ask
    drawn ask;                    // the first ask
    bool have_ask = false;
    for (size_t i = 0; i < f.states.size(); ++i) {
        const std::string state = "state " + std::to_string(i) + ": ";
        if (const std::string bad = rv_editor_check_pane_states(gui, f.shell, kind, { f.states[i] }, min); !bad.empty()) {
            return state + bad;
        }
        const drawn d = settled(f, min.w, min.h);
        if (i < f.first_ask) {
            if (kind == rv_editor_pane_kind::review_changes) {
                if (s13::content_area(d.record) == nullptr || s13::message_box(d.record) == nullptr) {
                    return state + "no content area or no message box";
                }
                if (i == 0) {
                    plain = d.record;
                } else if (const std::string moved = s13::windows_difference(plain, d.record); !moved.empty()) {
                    return state + moved;
                }
            }
            continue;
        }
        if (s13::has_ask_place(d.record)) {
            return state + "the old ask place is still drawn";
        }
        if (card_of(d) == nullptr) {
            return state + "the ask is not a card over the whole content region";
        }
        if (!have_ask) {
            ask = d;
            have_ask = true;
        } else if (const std::string moved = asks_difference(ask, d); !moved.empty()) {
            return state + moved;
        }
    }
    return "";
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_s18_every_state_fits_whole_at_the_minimum_and_the_asks_keep_their_places)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        if (min.w <= 0.0f || min.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own to draw in\n", name_of(kind));
            continue;
        }
        const std::string bad = run_states(gui, kind, min);
        if (!bad.empty()) {
            std::fprintf(stderr, "  %s at %.0fx%.0f: %s\n", name_of(kind), min.w, min.h, bad.c_str());
        }
        RV_EDITOR_CHECK(bad.empty());
    }
}

RV_EDITOR_TEST(rv_editor_test_s18_one_pixel_under_the_minimum_breaks_a_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        if (min.w <= 0.0f || min.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own to go under\n", name_of(kind));
            continue;
        }
        // The minimum is exact only if it passes and one pixel less does not.
        RV_EDITOR_CHECK(run_states(gui, kind, min).empty());
        const std::string narrow = run_states(gui, kind, { min.w - 1.0f, min.h });
        if (narrow.empty()) {
            std::fprintf(stderr, "  %s: no violation at the minimum width less 1 px\n", name_of(kind));
        }
        RV_EDITOR_CHECK(!narrow.empty());
        const std::string low = run_states(gui, kind, { min.w, min.h - 1.0f });
        if (low.empty()) {
            std::fprintf(stderr, "  %s: no violation at the minimum height less 1 px\n", name_of(kind));
        }
        RV_EDITOR_CHECK(!low.empty());
    }
}

// R12/AC13: the swap ask is a card too, and it fits under one tab row of the Code minimum.
RV_EDITOR_TEST(rv_editor_test_s18_swap_ask_fits_whole_in_the_code_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::code);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    float row = 0.0f;
    ImVec2 card{ min.w, min.h };
    gui.frame([&] {
        row = rv_editor_tab_strip_height() + ImGui::GetStyle().ItemSpacing.y;
        // No minimum yet: draw in the card's own size, so the checks below fail on the ask and not on an empty window.
        const ImVec2 own = rv_editor_mode_card_min_size(swap_buttons(), stable_tiles::theme);
        const ImVec2 pad = ImGui::GetStyle().WindowPadding;
        card = min.w > 0.0f && min.h > 0.0f ? ImVec2(min.w, min.h - row) : ImVec2(own.x + 2 * pad.x, own.y + 2 * pad.y);
    });
    const ImRect tile(0, 0, card.x, card.y);
    for (const auto view : { rv_editor_swap_view::recoverable, rv_editor_swap_view::in_use }) {
        drawn d;
        for (int i = 0; i < 3; ++i) {
            d.record = rv_editor_probe_draw(
                gui,
                card.x,
                [&](rv_editor_probe_record &) {
                    d.origin = ImGui::GetCursorScreenPos();
                    d.avail = ImGui::GetContentRegionAvail();
                    (void)rv_editor_code_swap_place(view, 12345, stable_tiles::theme);
                },
                card.y);
        }
        const std::string bad = rv_editor_fits_whole(d.record, tile);
        if (!bad.empty()) {
            std::fprintf(stderr, "  swap ask: %s\n", bad.c_str());
        }
        RV_EDITOR_CHECK(bad.empty());
        RV_EDITOR_CHECK(card_of(d) != nullptr);
        RV_EDITOR_CHECK(text_area(d.record) != nullptr);
        RV_EDITOR_CHECK(!s13::has_ask_place(d.record));
        if (view == rv_editor_swap_view::recoverable) {
            for (const std::string &b : swap_buttons()) {
                RV_EDITOR_CHECK(find(d.record, b) != nullptr);
            }
        }
    }
}
