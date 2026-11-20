// S14, R14, AC15: Release Controls is the transport template with five buttons: Build, Run, Run in Player, Stop and Export
// Report, then a right column of the action slot (Cancel Build or Force Stop) and a status line with the facts and the first
// check not passed, constant in size and clipping what does not fit. Under 640 px the buttons are small and
// carry no visible label, from 640 px they are large with one.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "panes/rv_editor_release_facts.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_s14.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s14;

namespace
{

constexpr auto kind = rv_editor_pane_kind::release_controls;
constexpr float labels_from = 640.0f;

const char *const button_keys[] = { "pane_candidate.build_button",
    "pane_candidate.run_button",
    "pane_candidate.player_button",
    "pane_candidate.stop_button",
    "pane_candidate.report_button" };

// Small tiles carry the name only as an id (`##name`), large ones show it.
const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label == label || e.label == "##" + label) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

// R14: five buttons in order on one row, all of one square size; from 640 px with a label, under it without; the slot
// after them in the same state-independent place.
RV_EDITOR_TEST(rv_editor_test_s14_release_has_five_buttons_whose_size_follows_the_width_alone)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(kind);
    const auto min = stable_tiles::pane_minimum(gui, kind);
    RV_EDITOR_CHECK(min.w > 0.0f);
    float small_side = 0.0f;
    gui.frame([&] {
        small_side = ImGui::GetFrameHeight();
    });
    const std::string cancel = rv_editor_text("pane_candidate.cancel_build_button");
    for (const float width : { min.w, 639.0f, 640.0f, 900.0f, 1280.0f }) {
        if (width < min.w) {
            continue;
        }
        const auto r = draw_at(gui, *all.shells[all.project_from + 1], kind, width, 400.0f);
        float side = 0.0f;
        for (size_t i = 0; i < std::size(button_keys); ++i) {
            const auto *b = find(r, rv_editor_text(button_keys[i]));
            RV_EDITOR_CHECK(b != nullptr);
            if (b == nullptr) {
                continue;
            }
            side = side == 0.0f ? b->x1 - b->x0 : side;
            RV_EDITOR_CHECK(std::fabs((b->x1 - b->x0) - side) <= 0.5f && std::fabs((b->y1 - b->y0) - side) <= 0.5f);
            const bool labelled = b->label.rfind("##", 0) != 0;
            if (labelled != (width >= labels_from)) {
                std::fprintf(stderr, "  width %.0f: button %zu '%s' labelled=%d\n", width, i, b->label.c_str(), labelled);
            }
            RV_EDITOR_CHECK(labelled == (width >= labels_from));
            const auto *first = find(r, rv_editor_text(button_keys[0]));
            RV_EDITOR_CHECK(first != nullptr && b->y0 == first->y0); // one row
        }
        if (width < labels_from) {
            RV_EDITOR_CHECK(std::fabs(side - small_side) <= 0.5f);
        } else {
            RV_EDITOR_CHECK(side > small_side);
        }
        const auto *slot = find(r, cancel);
        RV_EDITOR_CHECK(slot != nullptr);
        const auto *last = find(r, rv_editor_text(button_keys[4]));
        RV_EDITOR_CHECK(slot != nullptr && last != nullptr && slot->x0 >= last->x1);
    }
}

// AC15: the tile is as high as the strip drawn at its width, with no empty space under it; wider tiles with large buttons
// are higher. The strip's height is the height of the pane's own content at that width, whatever the state.
RV_EDITOR_TEST(rv_editor_test_s14_release_height_is_the_height_of_its_strip_at_every_width)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(kind);
    const auto min = stable_tiles::pane_minimum(gui, kind);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    float last = 0.0f;
    for (const float width : { min.w, 639.0f, 640.0f, 900.0f, 1280.0f }) {
        if (width < min.w) {
            continue;
        }
        float first_height = -1.0f;
        for (size_t i = all.project_from; i < all.shells.size(); ++i) {
            const float h = draw_at(gui, *all.shells[i], kind, width, 400.0f).content_height;
            first_height = first_height < 0.0f ? h : first_height;
            if (h != first_height) {
                std::fprintf(stderr,
                    "  width %.0f, state '%s': strip %.1f high, first state %.1f\n",
                    width,
                    all.names[i].c_str(),
                    h,
                    first_height);
            }
            RV_EDITOR_CHECK(h == first_height);
        }
        RV_EDITOR_CHECK(first_height >= last); // a wider tile never gets lower
        last = first_height;
    }
    RV_EDITOR_CHECK(std::ceil(draw_at(gui, *all.shells[all.project_from], kind, min.w, 400.0f).content_height) <= min.h + 0.5f);
    // Large buttons make the strip higher than small ones.
    RV_EDITOR_CHECK(draw_at(gui, *all.shells[all.project_from], kind, labels_from, 400.0f).content_height >
        draw_at(gui, *all.shells[all.project_from], kind, labels_from - 1.0f, 400.0f).content_height);
}

// R14, R11: the status is a window of one rectangle in every state; the longest facts (a building candidate with a six
// digit number) wrap whole into its two lines at the minimum width.
RV_EDITOR_TEST(rv_editor_test_s14_release_status_line_holds_the_longest_facts)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(kind);
    const auto min = stable_tiles::pane_minimum(gui, kind);
    RV_EDITOR_CHECK(min.w > 0.0f);
    const rv_editor_candidate longest = candidate(999999, false);
    const std::string facts = rv_editor_release_facts_make(&longest, rv_editor_release_building::candidate).first_line;
    float two = 0.0f;
    gui.frame([&] {
        two = 2.0f * ImGui::GetTextLineHeight();
    });
    for (const float width : { min.w, labels_from, 1280.0f }) {
        if (width < min.w) {
            continue;
        }
        const rv_editor_probe_entry *first = nullptr;
        rv_editor_probe_record first_record;
        for (size_t i = all.project_from; i < all.shells.size(); ++i) {
            const auto r = draw_at(gui, *all.shells[i], kind, width, 400.0f);
            const auto *row = child_named(r, "##status");
            if (row == nullptr) {
                std::fprintf(stderr, "  width %.0f, state '%s': no status line window\n", width, all.names[i].c_str());
            }
            RV_EDITOR_CHECK(row != nullptr);
            if (row == nullptr) {
                continue;
            }
            if (first == nullptr) {
                first_record = r;
                first = child_named(first_record, "##status");
            } else if (row->x0 != first->x0 || row->y0 != first->y0 || row->x1 != first->x1 || row->y1 != first->y1) {
                std::fprintf(stderr,
                    "  width %.0f, state '%s': status line %s became %s\n",
                    width,
                    all.names[i].c_str(),
                    rv_editor_probe_detail::rect(*first).c_str(),
                    rv_editor_probe_detail::rect(*row).c_str());
            }
            if (first != nullptr) {
                RV_EDITOR_CHECK(row->x0 == first->x0 && row->y0 == first->y0 && row->x1 == first->x1 && row->y1 == first->y1);
            }
            float need = 0.0f;
            const float room = row->x1 - row->x0;
            gui.frame([&] {
                need = ImGui::CalcTextSize(facts.c_str(), nullptr, false, room).y;
            });
            if (need > two + 0.5f) {
                std::fprintf(stderr,
                    "  width %.0f, state '%s': status %.1f px wide, the longest facts need %.1f px high of %.1f\n",
                    width,
                    all.names[i].c_str(),
                    room,
                    need,
                    two);
            }
            RV_EDITOR_CHECK(need <= two + 0.5f);
        }
    }
}

// R24: Cancel Build (Force Stop in a hung session) is in the slot in every state, disabled with its reason when there is
// nothing to cancel, and in the same place whichever of the two it shows.
RV_EDITOR_TEST(rv_editor_test_s14_release_action_slot_is_always_there)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(kind);
    const auto min = stable_tiles::pane_minimum(gui, kind);
    const std::string cancel = rv_editor_text("pane_candidate.cancel_build_button");
    const std::string force = rv_editor_text("pane_candidate.force_stop_button");
    const rv_editor_probe_entry *place = nullptr;
    rv_editor_probe_record first;
    for (size_t i = all.project_from; i < all.shells.size(); ++i) {
        const auto r = draw_at(gui, *all.shells[i], kind, std::max(min.w, 700.0f), 400.0f);
        const auto *slot = find(r, all.names[i] == "hung" ? force : cancel);
        if (slot == nullptr) {
            std::fprintf(stderr, "  state '%s': no action slot\n", all.names[i].c_str());
        }
        RV_EDITOR_CHECK(slot != nullptr);
        if (slot == nullptr) {
            continue;
        }
        if (place == nullptr) {
            first = r;
            place = find(first, all.names[i] == "hung" ? force : cancel);
        } else {
            RV_EDITOR_CHECK(slot->x0 == place->x0 && slot->y0 == place->y0 && slot->x1 == place->x1 && slot->y1 == place->y1);
        }
    }
}
