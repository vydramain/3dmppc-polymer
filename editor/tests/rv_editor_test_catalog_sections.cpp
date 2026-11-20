// S7: the Widget Catalog has a section for the titled sections of a content area, and each of its states fits whole at
// the minimum and the roomy tile and draws its title (and in the control states the controls) inside a content area.

#include <string>

#include "catalog/rv_editor_catalog.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;

int find_section()
{
    for (int s = 0; s < rv_editor_catalog_section_count(); ++s) {
        if (std::string(rv_editor_catalog_section_name(s)) == "Sections in a content area") {
            return s;
        }
    }
    return -1;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_catalog_has_a_section_for_content_sections)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    RV_EDITOR_CHECK(rv_editor_catalog_section_count() == 12);
    const int s = find_section();
    RV_EDITOR_CHECK(s >= 0);
    RV_EDITOR_CHECK(s < 0 || rv_editor_catalog_state_count(s) >= 2);
}

RV_EDITOR_TEST(rv_editor_test_catalog_content_sections_draw_in_an_area_and_fit_whole)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const int s = find_section();
    RV_EDITOR_CHECK(s >= 0);
    if (s < 0) {
        return;
    }
    rv_editor_test_imgui gui(true);
    ImVec2 sizes[2];
    gui.frame([&] {
        sizes[0] = rv_editor_catalog_tile_min(s, theme);
        sizes[1] = rv_editor_catalog_tile_roomy(s, theme);
    });
    for (const ImVec2 size : sizes) {
        for (int k = 0; k < rv_editor_catalog_state_count(s); ++k) {
            rv_editor_probe_record r;
            for (int i = 0; i < 3; ++i) {
                r = rv_editor_probe_draw(
                    gui,
                    size.x,
                    [&](rv_editor_probe_record &) {
                        rv_editor_catalog_tile_draw(s, k, theme);
                    },
                    size.y);
            }
            int in_area = 0;
            for (const auto &e : r.entries) {
                in_area += e.in_scroll ? 1 : 0;
            }
            RV_EDITOR_CHECK(in_area >= (k == 1 ? 3 : 1)); // title; in the control state also a field and a button
            RV_EDITOR_CHECK(rv_editor_fits_whole(r, ImRect(0, 0, size.x, size.y)).empty());
        }
    }
}
