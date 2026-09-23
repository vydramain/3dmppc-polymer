// Catalog section: the palette tokens and the drawing primitives.

#include <cstdint>
#include <initializer_list>

#include "catalog/rv_editor_catalog.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_icons.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_swatch
{
    const char *name;
    uint32_t rgb;
};

void rv_editor_catalog_palette(const rv_editor_theme &t)
{
    const rv_editor_swatch swatches[] = {
        {"window", t.window},
        {"inset", t.inset},
        {"button", t.button},
        {"selection", t.selection},
        {"bevel_hi", t.bevel_hi},
        {"bevel_lo", t.bevel_lo},
        {"text", t.text},
        {"text_bright", t.text_bright},
        {"text_disabled", t.text_disabled},
        {"dark", t.dark},
        {"code_base", t.code_base},
        {"error", t.error},
        {"warning", t.warning},
        {"ok", t.ok},
    };

    const float side = ImGui::GetFrameHeight();
    if (!ImGui::BeginTable("palette", 3)) {
        return;
    }
    for (const rv_editor_swatch &s : swatches) {
        ImGui::TableNextColumn();
        const ImVec2 min = rv_editor_catalog_reserve(ImVec2(side, side));
        rv_editor_draw_panel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + side, min.y + side), t, s.rgb,
            rv_editor_bevel::sunken);
        ImGui::SameLine();
        ImGui::Text("%s #%06x", s.name, static_cast<unsigned>(s.rgb));
    }
    ImGui::EndTable();
}

void rv_editor_catalog_primitives(const rv_editor_theme &t)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float cell = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemSpacing.x;

    ImVec2 min = rv_editor_catalog_reserve(ImVec2(cell * 4.0f, cell * 2.0f));
    rv_editor_draw_panel(dl, min, ImVec2(min.x + cell * 4.0f, min.y + cell * 2.0f), t, t.window,
        rv_editor_bevel::raised);
    ImGui::SameLine(0.0f, gap);
    min = rv_editor_catalog_reserve(ImVec2(cell * 4.0f, cell * 2.0f));
    rv_editor_draw_panel(dl, min, ImVec2(min.x + cell * 4.0f, min.y + cell * 2.0f), t, t.inset,
        rv_editor_bevel::sunken);
    ImGui::SameLine(0.0f, gap);
    min = rv_editor_catalog_reserve(ImVec2(cell * 8.0f, cell));
    rv_editor_draw_panel(dl, min, ImVec2(min.x + cell * 8.0f, min.y + cell), t, t.dark, rv_editor_bevel::raised);
    rv_editor_draw_stipple(dl, min, ImVec2(min.x + cell * 8.0f, min.y + cell), t, t.window);

    const ImGuiDir dirs[] = {ImGuiDir_Up, ImGuiDir_Down, ImGuiDir_Left, ImGuiDir_Right};
    for (ImGuiDir dir : dirs) {
        min = rv_editor_catalog_reserve(ImVec2(cell, cell));
        rv_editor_draw_panel(dl, min, ImVec2(min.x + cell, min.y + cell), t, t.button, rv_editor_bevel::raised);
        rv_editor_draw_arrow(dl, min, ImVec2(min.x + cell, min.y + cell), t, dir, t.text);
        ImGui::SameLine(0.0f, gap);
    }
    for (bool selected : {false, true}) {
        min = rv_editor_catalog_reserve(ImVec2(cell, cell));
        rv_editor_draw_diamond(dl, min, ImVec2(min.x + cell, min.y + cell), t, selected);
        ImGui::SameLine(0.0f, gap);
    }
    min = rv_editor_catalog_reserve(ImVec2(cell, cell));
    rv_editor_draw_panel(dl, min, ImVec2(min.x + cell, min.y + cell), t, t.inset, rv_editor_bevel::sunken);
    rv_editor_draw_check(dl, min, ImVec2(min.x + cell, min.y + cell), t, t.text);
    ImGui::SameLine(0.0f, gap);
    min = rv_editor_catalog_reserve(ImVec2(cell * 4.0f, cell));
    rv_editor_draw_panel(dl, min, ImVec2(min.x + cell * 4.0f, min.y + cell), t, t.button, rv_editor_bevel::raised);
    rv_editor_draw_focus(dl, min, ImVec2(min.x + cell * 4.0f, min.y + cell), t);
}

void rv_editor_catalog_icons(const rv_editor_theme &t)
{
    const float k = static_cast<float>(rv_editor_icon_scale(t.scale));
    for (int i = 0; i < static_cast<int>(rv_editor_icon_name::count); ++i) {
        const rv_editor_icon icon = rv_editor_icon_get(static_cast<rv_editor_icon_name>(i));
        if (icon.id == 0) {
            ImGui::TextUnformatted("(missing)");
        } else {
            ImGui::Image(ImTextureRef(icon.id), ImVec2(static_cast<float>(icon.w) * k, static_cast<float>(icon.h) * k));
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
}

} // namespace

void rv_editor_catalog_theme(const rv_editor_theme &theme)
{
    ImGui::SeparatorText("Palette");
    rv_editor_catalog_palette(theme);
    ImGui::SeparatorText("Primitives");
    rv_editor_catalog_primitives(theme);
    ImGui::SeparatorText("Icons");
    rv_editor_catalog_icons(theme);
    ImGui::TextUnformatted("Cyrillic: \xd0\x9a\xd0\xb0\xd1\x82\xd0\xb0\xd0\xbb\xd0\xbe\xd0\xb3");
}

} // namespace rv_editor
