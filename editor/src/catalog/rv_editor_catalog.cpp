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
    rv_editor_catalog_theme(theme);
    rv_editor_catalog_buttons(theme);
    rv_editor_catalog_fields(theme);
    rv_editor_catalog_panes(theme);
    rv_editor_catalog_status(theme);
}

} // namespace rv_editor
