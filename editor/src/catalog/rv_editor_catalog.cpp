#include "catalog/rv_editor_catalog.hpp"

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
    // The spec's twelve sections, in its order (CAT-01).
    ImGui::SeparatorText("1. Pane headers, window buttons, splitters and focus");
    rv_editor_catalog_headers(theme);
    ImGui::SeparatorText("2. Buttons, toggles and toolbar icons in every state");
    rv_editor_catalog_buttons(theme);
    ImGui::SeparatorText("3. Text field, spinner, dropdown: invalid, read-only, dirty");
    rv_editor_catalog_fields(theme);
    ImGui::SeparatorText("4. Checkbox, diamond radio and the keyboard");
    rv_editor_catalog_keys(theme);
    ImGui::SeparatorText("5. Tree, list and table: selection, opening, long names");
    rv_editor_catalog_lists(theme);
    ImGui::SeparatorText("6. Scrollbars, tabs and overflow");
    rv_editor_catalog_tab_strips(theme);
    rv_editor_catalog_overflow(theme);
    ImGui::SeparatorText("7. Transport and Stopped, Building, Running, Paused, Pending, Error");
    rv_editor_catalog_transports(theme);
    rv_editor_catalog_lamps(theme);
    ImGui::SeparatorText("8. Log: severity, sources, long lines");
    rv_editor_catalog_logs(theme);
    ImGui::SeparatorText("9. Code: Lua and C++, Cyrillic, cursor, selection, diagnostics");
    rv_editor_catalog_code(theme);
    ImGui::SeparatorText("10. Game test frame: native aspect, Fit, Integer, stale");
    rv_editor_catalog_game(theme);
    ImGui::SeparatorText("11. Icons, catalog cells, drop pocket, a picture that failed");
    rv_editor_catalog_icon_set(theme);
    rv_editor_catalog_cells(theme);
    ImGui::SeparatorText("12. Palette, fonts, UI scale, read-only against editable, thumbwheel");
    rv_editor_catalog_colours(theme);
    rv_editor_catalog_type(theme);
    ImGui::SeparatorText("Menus and dialogs");
    rv_editor_catalog_menus_dialogs(theme);
}

} // namespace rv_editor
