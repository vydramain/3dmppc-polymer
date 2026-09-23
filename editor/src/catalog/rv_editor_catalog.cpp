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
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    // Taller than the window once every group is in: it scrolls.
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Widget Catalog", nullptr, flags);
    ImGui::TextUnformatted("Widget Catalog");
    rv_editor_catalog_theme(theme);
    rv_editor_catalog_buttons(theme);
    ImGui::End();
}

} // namespace rv_editor
