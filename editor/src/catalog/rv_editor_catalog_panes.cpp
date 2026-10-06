// Catalog section: tree, list, table, tab strip, splitter and pane headers.

#include "catalog/rv_editor_catalog.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Splitter container height in frame heights (GetFrameHeight() multiplier)
constexpr float splitter_height_frames = 4.0f;

// Minimum pane height in frame heights (GetFrameHeight() multiplier) in a splitter
constexpr float splitter_min_pane_height_frames = 2.0f;

// Catalog tile window display area height in frame heights (GetFrameHeight() multiplier)
constexpr float catalog_tile_height_frames = 8.0f;

// Catalog lists and well view height in frame heights (GetFrameHeight() multiplier)
constexpr float catalog_view_height_frames = 6.0f;

// Number of columns in the catalog demo table (rows[][N])
constexpr int catalog_table_columns = 2;

// Number of columns in the catalog panes overview table
constexpr int catalog_panes_table_columns = 3;

// Initial left pane share of splitter width in the demo section
constexpr float splitter_demo_initial_left_share = 0.5f;

// Divisor for calculating half-heights in splitter initialization
constexpr float splitter_half_divisor = 2.0f;

// Halves the free space to centre the bar.
constexpr float half_divisor = 2.0f;

struct rv_editor_pane_values
{
    int selected = 1;
    int tab = 0;
    float left_share = splitter_demo_initial_left_share;
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
    for (int i = 0; i < static_cast<int>(std::size(items)); ++i) {
        ImGui::BeginDisabled(i == static_cast<int>(std::size(items)) - 1);
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
    if (!ImGui::BeginTable("##table", catalog_table_columns, flags)) {
        return;
    }
    ImGui::TableSetupColumn(rv_editor_text("catalog_panes.column_name"));
    ImGui::TableSetupColumn(rv_editor_text("catalog_panes.column_directory"));
    ImGui::TableHeadersRow();
    const char *rows[][catalog_table_columns] = {
        { "solid-maid", "~/Projects" },
        { "example-lua", "mppcdiscs" },
        { "example-cpp", "mppcdiscs" }
    };
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
    const char *const labels[] = { rv_editor_text("catalog_panes.tab_project"),
        rv_editor_text("catalog_panes.tab_scene"), rv_editor_text("catalog_panes.tab_assets"),
        rv_editor_text("catalog_panes.tab_console_output") };
    rv_editor_tab_strip("##tabs", labels, static_cast<int>(std::size(labels)), &rv_editor_pane_data.tab, t);
}

void rv_editor_catalog_splitters(const rv_editor_theme &t)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() * splitter_height_frames;
    const float bar = static_cast<float>(t.pad_px * t.scale);
    rv_editor_pane_values &v = rv_editor_pane_data;
    if (v.top <= 0.0f) {
        v.top = (height - bar) / half_divisor;
        v.bottom = height - bar - v.top;
    }
    const float min = ImGui::GetFrameHeight() * splitter_min_pane_height_frames;

    // Widths follow the space available each frame (it shrinks when the
    // scrollbar appears); the splitter moves the share, not a pixel count.
    const float span = avail - bar;
    float left = span * v.left_share;
    float right = span - left;

    ImGui::BeginChild("##left", ImVec2(left, height), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(rv_editor_text("catalog_panes.drag_bar"));
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    if (rv_editor_splitter("##split_x", rv_editor_axis::x, height, &left, &right, min, min, t)) {
        v.left_share = left / span;
    }
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginGroup();
    ImGui::BeginChild("##top", ImVec2(right, v.top), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(rv_editor_text("catalog_panes.split_top"));
    ImGui::EndChild();
    rv_editor_splitter("##split_y", rv_editor_axis::y, right, &v.top, &v.bottom, min / splitter_half_divisor,
        min / splitter_half_divisor, t);
    ImGui::BeginChild("##bottom", ImVec2(right, v.bottom), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(rv_editor_text("catalog_panes.split_bottom"));
    ImGui::EndChild();
    ImGui::EndGroup();
}

// Pane content for the tile demo: the pane's own name.
void rv_editor_catalog_pane_name(void *, rv_editor_pane_id, rv_editor_pane_kind kind, const rv_editor_theme &)
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
    ImGui::SeparatorText(rv_editor_text("catalog_panes.section_tile_windows"));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const rv_editor_rect area{
        static_cast<int>(at.x),
        static_cast<int>(at.y),
        static_cast<int>(ImGui::GetContentRegionAvail().x),
        static_cast<int>(ImGui::GetFrameHeight() * catalog_tile_height_frames)
    };
    rv_editor_workspace_draw(ws, t, rv_editor_catalog_pane_name, nullptr, nullptr, area);
}

// Shelf over well: how a pane's three layers (header, shelf, well) look
// stacked, so the tone and bevels can be checked by eye.
void rv_editor_catalog_layers(const rv_editor_theme &t)
{
    ImGui::SeparatorText(rv_editor_text("catalog_panes.section_shelf_well"));
    rv_editor_pane_header(rv_editor_text("catalog_panes.header_files"), true, t, true);
    rv_editor_shelf_begin("##shelf", t);
    if (rv_editor_letter_button(
            "##new", rv_editor_glyph::new_, t.code_green, rv_editor_text("catalog_panes.button_new_file"), t)) {
    }
    rv_editor_flow(ImGui::GetFrameHeight());
    if (rv_editor_letter_button("##del", rv_editor_glyph::delete_, t.code_red,
            rv_editor_text("catalog_panes.button_delete"), t)) {
    }
    rv_editor_shelf_end();
    if (rv_editor_well_begin("##well", ImVec2(0.0f, ImGui::GetFrameHeight() * catalog_view_height_frames), t)) {
        rv_editor_catalog_tree();
    }
    rv_editor_well_end();
}

} // namespace

void rv_editor_catalog_headers(const rv_editor_theme &theme)
{
    rv_editor_pane_header(rv_editor_text("catalog_panes.header_hierarchy"), true, theme, true);
    rv_editor_pane_header(rv_editor_text("catalog_panes.header_inspector"), false, theme, true);
    rv_editor_pane_header(rv_editor_text("catalog_panes.header_boxes_hovered"), true, theme, true,
        { rv_editor_look::hovered });
    rv_editor_pane_header(rv_editor_text("catalog_panes.header_boxes_pressed"), true, theme, true,
        { rv_editor_look::pressed });
    rv_editor_pane_header(rv_editor_text("catalog_panes.header_boxes_focused"), true, theme, true,
        { rv_editor_look::focused });
    rv_editor_catalog_splitters(theme);
    rv_editor_catalog_tiles(theme);
    rv_editor_catalog_layers(theme);
}

void rv_editor_catalog_lists(const rv_editor_theme &)
{
    const float height = ImGui::GetFrameHeight() * catalog_view_height_frames;
    if (ImGui::BeginTable("##panes", catalog_panes_table_columns)) {
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
}

void rv_editor_catalog_tab_strips(const rv_editor_theme &theme)
{
    rv_editor_catalog_tabs(theme);
}

} // namespace rv_editor
