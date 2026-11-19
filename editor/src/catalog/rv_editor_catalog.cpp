#include "catalog/rv_editor_catalog.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

ImVec2 rv_editor_catalog_reserve(ImVec2 size)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    return min;
}

void rv_editor_catalog_draw(const rv_editor_theme &theme)
{
    // The page scrolls as a whole; the spec's twelve sections, then the template sections.
    rv_editor_scroll_begin("##catalog", ImVec2(0.0f, 0.0f), true);
    ImGui::SeparatorText(rv_editor_text("catalog.section_1_pane_headers"));
    rv_editor_catalog_headers(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_2_buttons"));
    rv_editor_catalog_buttons(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_3_text_fields"));
    rv_editor_catalog_fields(theme);
    rv_editor_catalog_text_samples(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_4_checkbox_radio"));
    rv_editor_catalog_keys(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_5_tree_list"));
    rv_editor_catalog_tree_list(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_6_scrollbars_tabs"));
    rv_editor_catalog_scroll(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_8_log"));
    rv_editor_catalog_log(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_7_transport"));
    rv_editor_catalog_lamps(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_9_code"));
    rv_editor_catalog_code(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_10_game_frame"));
    rv_editor_catalog_game(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_11_icons_catalog"));
    rv_editor_catalog_icon_set(theme);
    rv_editor_catalog_cells(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_12_palette_fonts"));
    rv_editor_catalog_colours(theme);
    rv_editor_catalog_type(theme);
    ImGui::SeparatorText(rv_editor_text("catalog.section_menus_dialogs"));
    rv_editor_catalog_menu_samples();
    for (int section = 0; section < rv_editor_catalog_section_count(); ++section) {
        rv_editor_catalog_section_draw(section, theme);
    }
    rv_editor_scroll_end(theme);
}

} // namespace rv_editor
