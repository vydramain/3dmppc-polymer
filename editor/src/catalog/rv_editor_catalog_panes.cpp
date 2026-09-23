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

void rv_editor_catalog_tabs()
{
    if (!ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_DrawSelectedOverline)) {
        return;
    }
    if (ImGui::BeginTabItem("Scene")) {
        ImGui::TextUnformatted("Scene tab content");
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Code")) {
        ImGui::TextUnformatted("Code tab content");
        ImGui::EndTabItem();
    }
    ImGui::BeginDisabled();
    if (ImGui::BeginTabItem("Game")) {
        ImGui::EndTabItem();
    }
    ImGui::EndDisabled();
    ImGui::EndTabBar();
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

} // namespace

void rv_editor_catalog_panes(const rv_editor_theme &theme)
{
    ImGui::SeparatorText("Panes");
    rv_editor_pane_header("Hierarchy", true, theme);
    rv_editor_pane_header("Inspector", false, theme);

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
    rv_editor_catalog_tabs();
    rv_editor_catalog_splitters(theme);
}

} // namespace rv_editor
