// Catalog section: tree, list, table, tab strip, splitter and pane headers.

#include "catalog/rv_editor_catalog.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_pane_values
{
    int selected = 1;
    int tab = 0;
    float left_share = 0.5f; // splitter demo: the left pane's share of the width
    float top = 0.0f;        // splitter demo heights, set on first use
    float bottom = 0.0f;
};

rv_editor_pane_values rv_editor_pane_data;

void rv_editor_catalog_tree()
{
    constexpr ImGuiTreeNodeFlags open = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull;
    constexpr ImGuiTreeNodeFlags leaf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (!ImGui::TreeNodeEx("my-game/", open)) {
        return;
    }
    ImGui::TreeNodeEx("disc.toml", leaf | ImGuiTreeNodeFlags_Selected);
    ImGui::TreeNodeEx("main.lua", leaf);
    if (ImGui::TreeNodeEx("assets/", open)) {
        ImGui::TreeNodeEx("tone.pcm", leaf);
        ImGui::BeginDisabled();
        ImGui::TreeNodeEx("broken.png", leaf);
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
    ImGui::TreePop();
}

void rv_editor_catalog_list(float height)
{
    const char *items[] = {"solid-maid", "example-lua", "example-cpp", "missing-disc"};
    if (!ImGui::BeginListBox("##list", ImVec2(-1.0f, height))) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        ImGui::BeginDisabled(i == 3);
        if (ImGui::Selectable(items[i], rv_editor_pane_data.selected == i)) {
            rv_editor_pane_data.selected = i;
        }
        ImGui::EndDisabled();
    }
    ImGui::EndListBox();
}

void rv_editor_catalog_table()
{
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
    if (!ImGui::BeginTable("##table", 2, flags)) {
        return;
    }
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Directory");
    ImGui::TableHeadersRow();
    const char *rows[][2] = {{"solid-maid", "~/Projects"}, {"example-lua", "mppcdiscs"}, {"example-cpp", "mppcdiscs"}};
    for (const auto &row : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row[0]);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row[1]);
    }
    ImGui::EndTable();
}

void rv_editor_catalog_tabs(const rv_editor_theme &t)
{
    const char *const labels[] = {"Project", "Scene", "Assets", "Console"};
    rv_editor_tab_strip("##tabs", labels, 4, &rv_editor_pane_data.tab, t);
}

void rv_editor_catalog_splitters(const rv_editor_theme &t)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() * 4.0f;
    const float bar = static_cast<float>(t.pad_px * t.scale);
    rv_editor_pane_values &v = rv_editor_pane_data;
    if (v.top <= 0.0f) {
        v.top = (height - bar) / 2.0f;
        v.bottom = height - bar - v.top;
    }
    const float min = ImGui::GetFrameHeight() * 2.0f;

    // Widths follow the space available each frame (it shrinks when the
    // scrollbar appears); the splitter moves the share, not a pixel count.
    const float span = avail - bar;
    float left = span * v.left_share;
    float right = span - left;

    ImGui::BeginChild("##left", ImVec2(left, height), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Drag the bar");
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    if (rv_editor_splitter("##split_x", rv_editor_axis::x, height, &left, &right, min, min, t)) {
        v.left_share = left / span;
    }
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginGroup();
    ImGui::BeginChild("##top", ImVec2(right, v.top), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Top");
    ImGui::EndChild();
    rv_editor_splitter("##split_y", rv_editor_axis::y, right, &v.top, &v.bottom, min / 2.0f, min / 2.0f, t);
    ImGui::BeginChild("##bottom", ImVec2(right, v.bottom), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Bottom");
    ImGui::EndChild();
    ImGui::EndGroup();
}

// Pane content for the tile demo: the pane's own name.
void rv_editor_catalog_pane_name(rv_editor_pane_id, rv_editor_pane_kind kind, const rv_editor_theme &)
{
    ImGui::TextUnformatted(rv_editor_pane_title(kind));
}

// A live workspace of three panes, so the tile window is checked as the editor
// draws it: frame, header boxes, content well and folder tabs.
void rv_editor_catalog_tiles(const rv_editor_theme &t)
{
    static rv_editor_workspace ws = [] {
        rv_editor_workspace w;
        const rv_editor_pane_id code = rv_editor_pane_add(w.panes, rv_editor_pane_kind::code);
        const rv_editor_pane_id output = rv_editor_pane_add(w.panes, rv_editor_pane_kind::output);
        const rv_editor_pane_id terminal = rv_editor_pane_add(w.panes, rv_editor_pane_kind::terminal);
        w.layout = rv_editor_layout_make(code);
        rv_editor_tile_insert(w.layout, rv_editor_tile_find(w.layout, code), output, rv_editor_tile_dock::right);
        rv_editor_tile_insert(w.layout, rv_editor_tile_find(w.layout, output), terminal, rv_editor_tile_dock::tab);
        return w;
    }();
    ImGui::SeparatorText("Tile windows");
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const rv_editor_rect area{ static_cast<int>(at.x), static_cast<int>(at.y),
        static_cast<int>(ImGui::GetContentRegionAvail().x), static_cast<int>(ImGui::GetFrameHeight() * 8.0f) };
    rv_editor_workspace_draw(ws, t, rv_editor_catalog_pane_name, area);
}

} // namespace

void rv_editor_catalog_panes(const rv_editor_theme &theme)
{
    ImGui::SeparatorText("Panes");
    rv_editor_pane_header("Hierarchy", true, theme, true);
    rv_editor_pane_header("Inspector", false, theme, true);
    rv_editor_pane_header("Boxes hovered", true, theme, true, { rv_editor_look::hovered });
    rv_editor_pane_header("Boxes pressed", true, theme, true, { rv_editor_look::pressed });
    rv_editor_pane_header("Boxes focused", true, theme, true, { rv_editor_look::focused });

    const float height = ImGui::GetFrameHeight() * 6.0f;
    if (ImGui::BeginTable("##panes", 3)) {
        ImGui::TableNextColumn();
        ImGui::BeginChild("##tree", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
        rv_editor_catalog_tree();
        ImGui::EndChild();
        ImGui::TableNextColumn();
        rv_editor_catalog_list(height);
        ImGui::TableNextColumn();
        rv_editor_catalog_table();
        ImGui::EndTable();
    }
    rv_editor_catalog_tabs(theme);
    rv_editor_catalog_splitters(theme);
    rv_editor_catalog_tiles(theme);
}

} // namespace rv_editor
