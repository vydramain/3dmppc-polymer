// S22, found in the live editor with the real fonts: Assets shows the whole name under every tile of the grid and a folder
// drop-down as wide as its text (R2); the areas of Session, Release Candidate and Observe without data say what will appear
// there and how (R4).

#include <cstdio>
#include <fstream>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"
#include "rv_editor_test_s17.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr float roomy_w = 900.0f;
constexpr float roomy_h = 700.0f;
constexpr int name_chars[] = { 20, 25, 30 };
constexpr const char *long_folder = "assets_with_a_long_folder_name";

// Files whose names are 20, 25 and 30 'Z' (a letter no other text of the pane has), and a second folder with a long name.
void make_long_names(const s15::fs::path &root)
{
    s15::fs::create_directories(root / long_folder);
    for (const int n : name_chars) {
        std::ofstream(root / long_folder / (std::string(static_cast<size_t>(n), 'Z') + ".txt")) << "words";
    }
}

} // namespace

// R2: the name under a tile is drawn whole, not cut to the tile: every one of the 75 letters of the three names is drawn.
RV_EDITOR_TEST(rv_editor_test_s22_assets_grid_draws_the_whole_name_of_every_tile)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    const auto root = f.where / "long";
    make_long_names(root);
    f.open_project(root);
    f.app().assets_ui.filter[0] = '\0';
    f.app().assets_ui.folder = 0;
    f.app().assets_ui.details = false;
    int letters = 0;
    for (const int n : name_chars) {
        letters += n;
    }
    for (const float width : { roomy_w, 600.0f }) {
        const int drawn = s15::glyph_counts(f, width, roomy_h, "Z")[0];
        std::fprintf(stderr, "  width %.0f: %d of %d letters drawn\n", width, drawn, letters);
        RV_EDITOR_CHECK(drawn == letters);
    }
}

// R2: the folder drop-down is not narrower than the text it shows (the value "All folders" and a long folder name), at
// the pane's minimum and at a roomy size.
RV_EDITOR_TEST(rv_editor_test_s22_assets_folder_dropdown_is_as_wide_as_its_value)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    const auto root = f.where / "long";
    make_long_names(root);
    f.open_project(root);
    f.app().assets_ui.filter[0] = '\0';
    float pad = 0.0f;
    float all_w = 0.0f;
    float long_w = 0.0f;
    gui.frame([&] {
        pad = ImGui::GetStyle().FramePadding.x;
        all_w = ImGui::CalcTextSize(rv_editor_text("pane_assets.all_folders")).x;
        long_w = ImGui::CalcTextSize(long_folder).x;
    });
    for (const ImVec2 size : s15::tile_sizes(gui, f.kind, ImVec2(roomy_w, roomy_h))) {
        for (const int folder : { 0, 1 }) {
            f.app().assets_ui.folder = folder;
            const auto r = f.settled(size.x, size.y);
            const auto *drop = s15::find_id(r, "##folder");
            RV_EDITOR_CHECK(drop != nullptr);
            if (drop != nullptr) {
                const float need = (folder == 0 ? all_w : long_w) + 2.0f * pad;
                std::fprintf(stderr,
                    "  tile %.0f, folder %d: dropdown %.1f wide, its text needs %.1f\n",
                    size.x,
                    folder,
                    drop->x1 - drop->x0,
                    need);
                RV_EDITOR_CHECK(drop->x1 - drop->x0 >= need);
            }
        }
    }
}

// R4: where no data exists yet, every content area of the view says in its first row what will appear and how: Session
// without a session, Release Candidate without candidates, Observe without a session.
RV_EDITOR_TEST(rv_editor_test_s22_empty_areas_of_session_candidate_and_observe_say_what_will_appear)
{
    s17::load_texts();
    rv_editor_test_imgui gui(true);
    struct empty_state {
        rv_editor_pane_kind kind;
        size_t state;
        size_t area_count;
    };
    for (const empty_state &s : { empty_state{ rv_editor_pane_kind::session, 1, 2 },
             empty_state{ rv_editor_pane_kind::candidate, 1, 3 },
             empty_state{ rv_editor_pane_kind::observe, 1, 3 } }) {
        const auto r = s17::at_roomy(gui, s.kind, s.state);
        const auto areas = s17::areas(r);
        RV_EDITOR_CHECK(areas.size() == s.area_count);
        for (size_t i = 0; i < areas.size(); ++i) {
            // The scroll wrapper and fillers carry no text and may start above the row; the first row is the topmost item with
            // a text. A long row may stick out to the right of the area (R2), so only its top-left corner must be inside.
            const rv_editor_probe_entry *first = nullptr;
            for (const auto &e : r.entries) {
                const float cy = (e.y0 + e.y1) / 2.0f;
                const bool row = e.in_scroll && !e.scroll_area && e.text_w > 0.0f && e.x0 >= areas[i]->x0 &&
                    cy >= areas[i]->y0 && cy <= areas[i]->y1;
                first = row && (first == nullptr || e.y0 < first->y0) ? &e : first;
            }
            const bool says = first != nullptr;
            std::fprintf(stderr, "  %s area %zu: first row %s\n", s17::name_of(s.kind), i, says ? "has a text" : "is missing");
            RV_EDITOR_CHECK(says);
        }
    }
}
