// S12: the Widget Catalog has a section for each template R7-R17 (tabs, shelf, list, records, message box, big ask, form,
// transport, picture, facts, status bar). Each has a state switcher and two shows: a tile at the template's minimum and
// a roomy one. In every state and at both sizes the tile fits whole.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>

#include "catalog/rv_editor_catalog.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr int sections = 12;
// The states of each template as the templates page lists them.
constexpr int state_counts[sections] = { 4, 3, 4, 4, 6, 4, 4, 5, 4, 3, 6, 3 };

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

struct sizes {
    ImVec2 min, roomy;
};

sizes tile_sizes(rv_editor_test_imgui &gui, int section)
{
    sizes s{};
    gui.frame([&] {
        s.min = rv_editor_catalog_tile_min(section, theme);
        s.roomy = rv_editor_catalog_tile_roomy(section, theme);
    });
    return s;
}

rv_editor_probe_record draw_tile(rv_editor_test_imgui &gui, int section, int state, ImVec2 size)
{
    rv_editor_probe_record r;
    for (int i = 0; i < 3; ++i) { // a scroll area and a child decide their bars from the previous frame
        r = rv_editor_probe_draw(
            gui,
            size.x,
            [&](rv_editor_probe_record &) {
                rv_editor_catalog_tile_draw(section, state, theme);
            },
            size.y);
    }
    return r;
}

const rv_editor_probe_entry *find_label(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label.substr(0, e.label.find("##")) == label) {
            return &e;
        }
    }
    return nullptr;
}

bool has_size(const rv_editor_probe_record &r, ImVec2 size)
{
    for (const auto &e : r.entries) {
        if (std::fabs((e.x1 - e.x0) - size.x) < 0.5f && std::fabs((e.y1 - e.y0) - size.y) < 0.5f) {
            return true;
        }
    }
    return false;
}

} // namespace

// R18: twelve sections, named, each with the states of its template.
RV_EDITOR_TEST(rv_editor_test_catalog_has_a_section_per_template)
{
    load_texts();
    RV_EDITOR_CHECK(rv_editor_catalog_section_count() == sections);
    std::set<std::string> names;
    for (int s = 0; s < sections; ++s) {
        RV_EDITOR_CHECK(rv_editor_catalog_state_count(s) == state_counts[s]);
        RV_EDITOR_CHECK(std::string(rv_editor_catalog_section_name(s)) != "");
        names.insert(rv_editor_catalog_section_name(s));
        std::set<std::string> states;
        for (int k = 0; k < state_counts[s]; ++k) {
            const std::string name = rv_editor_catalog_state_name(s, k);
            RV_EDITOR_CHECK(!name.empty());
            states.insert(name);
        }
        RV_EDITOR_CHECK(static_cast<int>(states.size()) == state_counts[s]); // distinct names for the switcher
    }
    RV_EDITOR_CHECK(static_cast<int>(names.size()) == sections);
}

// The message box section lists the six states of the page, in its order.
RV_EDITOR_TEST(rv_editor_test_catalog_message_section_has_six_states)
{
    load_texts();
    RV_EDITOR_CHECK(rv_editor_catalog_state_count(4) == 6);
}

// S1: section 4 is the message strip, not the box with its reserved place: its name says so, and the state "nothing to
// say" draws no strip while a message state does.
RV_EDITOR_TEST(rv_editor_test_catalog_message_section_is_the_strip)
{
    load_texts();
    std::string name = rv_editor_catalog_section_name(4);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    RV_EDITOR_CHECK(name.find("strip") != std::string::npos);
    RV_EDITOR_CHECK(name.find("box") == std::string::npos);
    rv_editor_test_imgui gui(true);
    const sizes z = tile_sizes(gui, 4);
    const auto nothing = draw_tile(gui, 4, 0, z.roomy);
    const auto error = draw_tile(gui, 4, 2, z.roomy);
    RV_EDITOR_CHECK(error.entries.size() > nothing.entries.size());
}

// A roomy tile is larger than the minimum one on both axes, and the minimum is not empty.
RV_EDITOR_TEST(rv_editor_test_catalog_tile_sizes_are_min_and_roomy)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (int s = 0; s < sections; ++s) {
        const sizes z = tile_sizes(gui, s);
        RV_EDITOR_CHECK(z.min.x > 0.0f && z.min.y > 0.0f);
        RV_EDITOR_CHECK(z.roomy.x > z.min.x && z.roomy.y > z.min.y);
    }
}

// AC19: every section, every state, at the minimum and roomy: fits_whole.
RV_EDITOR_TEST(rv_editor_test_catalog_every_state_fits_whole_at_min_and_roomy)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (int s = 0; s < sections; ++s) {
        const sizes z = tile_sizes(gui, s);
        const ImVec2 shows[2] = { z.min, z.roomy };
        for (int show = 0; show < 2; ++show) {
            for (int k = 0; k < state_counts[s]; ++k) {
                const auto r = draw_tile(gui, s, k, shows[show]);
                const std::string bad = rv_editor_fits_whole(r, ImRect(0, 0, shows[show].x, shows[show].y));
                RV_EDITOR_CHECK(!r.entries.empty()); // the state draws something
                RV_EDITOR_CHECK(bad.empty());
                if (!bad.empty()) {
                    std::fprintf(stderr, "  section %d state %d %s: %s\n", s, k, show == 0 ? "min" : "roomy", bad.c_str());
                }
            }
        }
    }
}

// The section shows two tiles, one of exactly the minimum size and one of exactly the roomy size.
RV_EDITOR_TEST(rv_editor_test_catalog_section_shows_a_minimum_and_a_roomy_tile)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (int s = 0; s < sections; ++s) {
        const sizes z = tile_sizes(gui, s);
        rv_editor_catalog_set_state(s, 0);
        rv_editor_probe_record r;
        for (int i = 0; i < 3; ++i) {
            r = rv_editor_probe_draw(
                gui,
                1950.0f,
                [&](rv_editor_probe_record &) {
                    rv_editor_catalog_section_draw(s, theme);
                },
                1950.0f);
        }
        RV_EDITOR_CHECK(has_size(r, z.min));
        RV_EDITOR_CHECK(has_size(r, z.roomy));
    }
}

// The switcher is one item per state; pressing one makes it the section's state.
RV_EDITOR_TEST(rv_editor_test_catalog_switcher_changes_the_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (int s = 0; s < sections; ++s) {
        if (rv_editor_catalog_state_count(s) < 2) {
            RV_EDITOR_CHECK(false); // no states, nothing to switch
            continue;
        }
        const int last = rv_editor_catalog_state_count(s) - 1;
        rv_editor_catalog_set_state(s, 0);
        RV_EDITOR_CHECK(rv_editor_catalog_state(s) == 0);
        const auto body = [&](rv_editor_probe_record &) {
            rv_editor_catalog_section_draw(s, theme);
        };
        rv_editor_probe_record r;
        for (int i = 0; i < 3; ++i) {
            r = rv_editor_probe_draw(gui, 1950.0f, body, 1950.0f);
        }
        for (int k = 0; k <= last; ++k) {
            RV_EDITOR_CHECK(find_label(r, rv_editor_catalog_state_name(s, k)) != nullptr);
        }
        const auto *button = find_label(r, rv_editor_catalog_state_name(s, last));
        if (button == nullptr) {
            continue;
        }
        ImGui::GetIO().AddMousePosEvent((button->x0 + button->x1) / 2, (button->y0 + button->y1) / 2);
        rv_editor_probe_draw(gui, 1950.0f, body, 1950.0f);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        rv_editor_probe_draw(gui, 1950.0f, body, 1950.0f);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        rv_editor_probe_draw(gui, 1950.0f, body, 1950.0f);
        RV_EDITOR_CHECK(rv_editor_catalog_state(s) == last);
        ImGui::GetIO().AddMousePosEvent(-1000, -1000);
        rv_editor_probe_draw(gui, 1950.0f, body, 1950.0f);
    }
}

// AC19, S1: the catalog as a pane has a minimum and passes the S1 check at it, whatever state the sections are in.
RV_EDITOR_TEST(rv_editor_test_catalog_pane_fits_whole_at_its_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::catalog);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    if (min.w <= 0.0f || min.h <= 0.0f) {
        return;
    }
    rv_editor_shell shell;
    std::vector<std::function<void()>> states;
    for (int k = 0; k < 6; ++k) {
        states.push_back([k] {
            for (int s = 0; s < sections; ++s) {
                rv_editor_catalog_set_state(s, std::min(k, std::max(0, rv_editor_catalog_state_count(s) - 1)));
            }
        });
    }
    const std::string bad = rv_editor_check_pane_states(gui, shell, rv_editor_pane_kind::catalog, states, min);
    RV_EDITOR_CHECK(bad.empty());
    if (!bad.empty()) {
        std::fprintf(stderr, "  catalog at its minimum: %s\n", bad.c_str());
    }
}
