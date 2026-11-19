// The template sections of the Widget Catalog: one per template, each with a state switcher and two tiles.

#include "catalog/rv_editor_catalog.hpp"

#include <algorithm>
#include <cmath>

#include "catalog/rv_editor_catalog_templates.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

const rv_editor_catalog_template *const templates[] = {
    &rv_editor_catalog_template_tabs,
    &rv_editor_catalog_template_shelf,
    &rv_editor_catalog_template_list,
    &rv_editor_catalog_template_records,
    &rv_editor_catalog_template_message,
    &rv_editor_catalog_template_mode,
    &rv_editor_catalog_template_form,
    &rv_editor_catalog_template_transport,
    &rv_editor_catalog_template_picture,
    &rv_editor_catalog_template_facts,
    &rv_editor_catalog_template_status,
};
constexpr int section_total = static_cast<int>(std::size(templates));

int section_states[section_total] = {};

const rv_editor_catalog_template &of(int section)
{
    return *templates[std::clamp(section, 0, section_total - 1)];
}

// A tile is a bordered child of exactly its size that keeps the window padding, with no bars of its own.
void draw_tile(const char *id, ImVec2 size, int section, const rv_editor_theme &theme)
{
    // A tile out of view keeps its place and draws nothing: the page has a long run of them.
    if (!ImGui::IsRectVisible(size)) {
        ImGui::Dummy(size);
        return;
    }
    ImGui::BeginChild(id,
        size,
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    rv_editor_catalog_tile_draw(section, rv_editor_catalog_state(section), theme);
    ImGui::EndChild();
}

} // namespace

const char *rv_editor_catalog_tpl_text(const char *key)
{
    static std::string full;
    full = std::string("catalog_templates.") + key;
    return rv_editor_text(full);
}

ImVec2 rv_editor_catalog_tpl_window(ImVec2 content)
{
    const ImVec2 pad = ImGui::GetStyle().WindowPadding;
    return ImVec2(content.x + 2.0f * pad.x, content.y + 2.0f * pad.y);
}

ImVec2 rv_editor_catalog_tpl_roomy(ImVec2 min)
{
    return ImVec2(min.x * 1.8f + 4.0f * ImGui::GetFontSize(), min.y * 1.8f + 4.0f * ImGui::GetFontSize());
}

int rv_editor_catalog_section_count()
{
    return section_total;
}

const char *rv_editor_catalog_section_name(int section)
{
    return rv_editor_catalog_tpl_text(of(section).name);
}

int rv_editor_catalog_state_count(int section)
{
    return of(section).state_count;
}

const char *rv_editor_catalog_state_name(int section, int state)
{
    const rv_editor_catalog_template &t = of(section);
    return rv_editor_catalog_tpl_text(t.states[std::clamp(state, 0, t.state_count - 1)]);
}

int rv_editor_catalog_state(int section)
{
    return section_states[std::clamp(section, 0, section_total - 1)];
}

void rv_editor_catalog_set_state(int section, int state)
{
    section_states[std::clamp(section, 0, section_total - 1)] = std::clamp(state, 0, of(section).state_count - 1);
}

// ImGui floors a child's size to whole pixels, so the tiles are whole pixels to begin with.
ImVec2 rv_editor_catalog_tile_min(int section, const rv_editor_theme &theme)
{
    const ImVec2 size = of(section).tile_min(theme);
    return ImVec2(std::ceil(size.x), std::ceil(size.y));
}

ImVec2 rv_editor_catalog_tile_roomy(int section, const rv_editor_theme &theme)
{
    const ImVec2 size = of(section).tile_roomy(theme);
    return ImVec2(std::ceil(size.x), std::ceil(size.y));
}

void rv_editor_catalog_tile_draw(int section, int state, const rv_editor_theme &theme)
{
    of(section).draw(std::clamp(state, 0, of(section).state_count - 1), theme);
}

void rv_editor_catalog_section_draw(int section, const rv_editor_theme &theme)
{
    ImGui::PushID(section);
    rv_editor_section(rv_editor_catalog_section_name(section));
    for (int state = 0; state < rv_editor_catalog_state_count(section); ++state) {
        const char *name = rv_editor_catalog_state_name(section, state);
        if (state > 0) {
            rv_editor_flow(rv_editor_checkbox_width(name));
        }
        if (rv_editor_radio(name, state == rv_editor_catalog_state(section), theme)) {
            rv_editor_catalog_set_state(section, state);
        }
    }
    const ImVec2 min = rv_editor_catalog_tile_min(section, theme);
    const ImVec2 roomy = rv_editor_catalog_tile_roomy(section, theme);
    rv_editor_label(rv_editor_catalog_tpl_text("show_min"), rv_editor_label_kind::dim, theme, rv_editor_label_layout::line);
    draw_tile("##min", min, section, theme);
    rv_editor_label(rv_editor_catalog_tpl_text("show_roomy"), rv_editor_label_kind::dim, theme, rv_editor_label_layout::line);
    draw_tile("##roomy", roomy, section, theme);
    ImGui::PopID();
}

} // namespace rv_editor
