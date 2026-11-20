// S22.1, found in the live editor with the real fonts: a table is as wide as its area or wider
// (R9); a picked folder row of the Files tree keeps its icon readable on the highlight (R9); the section titles of the
// Widget Catalog are drawn whole in a narrow tile (R2).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "catalog/rv_editor_catalog.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_s15.hpp"
#include "app/rv_editor_shell.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_table.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr float wide_w = 1000.0f;
constexpr float wide_h = 500.0f;
constexpr float edge_slack = 2.0f;

// The table with `columns` columns that was drawn in the last frame, nullptr when there is none.
const ImGuiTable *drawn_table(int columns)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Tables.GetMapSize(); ++i) {
        const ImGuiTable *t = g.Tables.TryGetMapData(i);
        if (t != nullptr && t->ColumnsCount == columns && t->LastFrameActive == g.FrameCount && t->InnerWindow != nullptr) {
            return t;
        }
    }
    return nullptr;
}

// R9: "" when the table reaches the right edge of the visible area of its window, else how far short it falls.
std::string table_reach(const ImGuiTable *t)
{
    if (t == nullptr) {
        return "no table was drawn";
    }
    const float area = t->InnerWindow->InnerRect.Max.x;
    if (t->OuterRect.Max.x >= area - edge_slack) {
        return "";
    }
    return "the table ends at " + std::to_string(t->OuterRect.Max.x) + ", its area at " + std::to_string(area);
}

void report(const char *what, const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s: %s\n", what, bad.c_str());
    }
}

} // namespace

// R9, widget: a table of fixed-fit columns whose cells are shorter than the area still spans the area, so its header and
// row bands are as wide as the area.
RV_EDITOR_TEST(rv_editor_test_s22_1_table_widget_spans_a_wider_area)
{
    rv_editor_test_imgui gui(true);
    const rv_editor_table_column columns[3] = { { "Level" }, { "File" }, { "Message" } };
    const rv_editor_table_style style = { .row_bands = true, .header = true, .sizing = rv_editor_table_sizing::fixed_fit };
    for (int i = 0; i < 3; ++i) {
        gui.frame(
            [&] {
                if (rv_editor_table_begin("##t", columns, 3, style)) {
                    ImGui::TableNextRow();
                    for (int c = 0; c < 3; ++c) {
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted("x");
                    }
                    rv_editor_table_end();
                }
            },
            wide_w,
            wide_h);
    }
    const std::string bad = table_reach(drawn_table(3));
    report("table widget", bad);
    RV_EDITOR_CHECK(bad.empty());
}

// R9, Problems: with no problems and with short ones the header and the rows reach the right edge of the area.
RV_EDITOR_TEST(rv_editor_test_s22_1_problems_table_spans_the_area)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    f.states[1](); // a project, nothing found
    (void)s13::settled(f, wide_w, wide_h);
    const std::string bad_empty = table_reach(drawn_table(5));
    report("problems, none", bad_empty);
    RV_EDITOR_CHECK(bad_empty.empty());
    f.app().problems.push_back({ f.where / "a.cpp", 3, 5, true, "expected ';'", "build" });
    (void)s13::settled(f, wide_w, wide_h);
    const std::string bad_some = table_reach(drawn_table(5));
    report("problems, short rows", bad_some);
    RV_EDITOR_CHECK(bad_some.empty());
}

// R9, Problems as the live editor draws it: the Code layout in 1280x720 with the real fonts and the theme, a project open,
// no build yet, so the one row is the explanation. The header and that row reach the right edge of the content area.
RV_EDITOR_TEST(rv_editor_test_s22_1_problems_table_in_the_code_layout_spans_the_area)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    f.states[1](); // a project, no build, no alert
    f.shell.ws.panes = {};
    rv_editor_layout_preset_make(rv_editor_layout_preset::code, f.shell.ws.panes, f.shell.ws.layout);
    f.shell.ws.focused_leaf = f.shell.ws.layout.root;
    for (rv_editor_tile_node &node : f.shell.ws.layout.nodes) { // the Problems tab is the one shown
        for (size_t i = 0; i < node.leaf.tabs.size(); ++i) {
            if (node.kind == rv_editor_tile_kind::leaf &&
                f.shell.ws.panes.panes[node.leaf.tabs[i]].kind == rv_editor_pane_kind::inspector_report) {
                node.leaf.active = static_cast<uint32_t>(i);
            }
        }
    }
    constexpr int win_w = 1280;
    constexpr int win_h = 720;
    rv_editor_theme_apply(stable_tiles::theme, ImGui::GetStyle());
    ImGui::GetStyle().FontSizeBase = rv_editor_font_ui()->LegacySize;
    for (int i = 0; i < 6; ++i) {
        gui.frame(
            [&] {
                f.shell.ws.minimums.clear();
                for (size_t k = 0; k < f.shell.ws.panes.panes.size(); ++k) {
                    rv_editor_shell_set_minimum(f.shell,
                        static_cast<rv_editor_pane_id>(k),
                        f.shell.ws.panes.panes[k].kind,
                        { 0, 0 });
                }
                const int bar = static_cast<int>(ImGui::GetFrameHeight());
                std::string error;
                rv_editor_workspace_draw(
                    f.shell.ws,
                    stable_tiles::theme,
                    [](void *context, rv_editor_pane_id id, rv_editor_pane_kind kind, const rv_editor_theme &theme) {
                        rv_editor_shell_pane(static_cast<rv_editor_shell *>(context), id, kind, theme);
                    },
                    nullptr,
                    &f.shell,
                    { 0, bar, win_w, win_h - 2 * bar },
                    error);
            },
            static_cast<float>(win_w),
            static_cast<float>(win_h));
    }
    const ImGuiTable *t = drawn_table(5);
    RV_EDITOR_CHECK(t != nullptr);
    float area = 0.0f; // the right edge of the content area, whose scroll window holds the table
    for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        if (w->Active && std::string(w->Name).find("##area") != std::string::npos) {
            std::fprintf(stderr, "  window %s inner right %.1f\n", w->Name, w->InnerRect.Max.x);
            area = std::max(area, w->InnerRect.Max.x);
        }
    }
    RV_EDITOR_CHECK(area > 0.0f);
    if (t == nullptr) {
        return;
    }
    // The bands drawn (flat fills) in the first rows of the table, by colour: the header's and the explanation row's.
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    std::map<ImU32, float> reach;
    for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        if (w != t->OuterWindow && w != t->InnerWindow) {
            continue;
        }
        for (const ImDrawVert &v : w->DrawList->VtxBuffer) {
            const bool flat = std::abs(v.uv.x - white.x) < 1e-6f && std::abs(v.uv.y - white.y) < 1e-6f;
            if (flat && v.pos.y >= t->OuterRect.Min.y && v.pos.y <= t->OuterRect.Min.y + 2.0f * ImGui::GetFrameHeight()) {
                reach[v.col] = std::max(reach[v.col], v.pos.x);
            }
        }
    }
    RV_EDITOR_CHECK(reach.size() >= 2); // the header band and the row band
    for (const auto &[col, right] : reach) {
        std::fprintf(stderr, "  code layout: a band of colour %08x ends at %.1f, its area at %.1f\n", col, right, area);
        RV_EDITOR_CHECK(right >= area - edge_slack);
    }
}

// R9, Search: short hits make a table as wide as the area, too.
RV_EDITOR_TEST(rv_editor_test_s22_1_search_table_spans_the_area)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    s13::fixture f(gui, rv_editor_pane_kind::search);
    f.states[1]();
    f.app().project_search.searched = "needle";
    f.app().project_search.result = {};
    f.app().project_search.result.files = 1;
    f.app().project_search.result.hits.push_back({ f.where / "a.cpp", 1, 4, "needle" });
    (void)s13::settled(f, wide_w, wide_h);
    const std::string bad = table_reach(drawn_table(3));
    report("search", bad);
    RV_EDITOR_CHECK(bad.empty());
}

// R9: on the highlight of the picked folder row of the Files tree nothing is drawn in the highlight's own colour: the
// folder icon ("Fd", now the colour of the selection) and the name stay readable.
RV_EDITOR_TEST(rv_editor_test_s22_1_files_picked_folder_icon_differs_from_the_highlight)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    rv_editor_theme_apply(stable_tiles::theme, ImGui::GetStyle());
    s15::fixture f(gui, rv_editor_pane_kind::files);
    f.open_files();
    f.shell.ui.files_selected = f.where / "sub";
    (void)f.settled(600.0f, 420.0f);
    const ImU32 header = ImGui::GetColorU32(ImGuiCol_Header);
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    const auto rgb = [](ImU32 c) {
        return c & 0x00FFFFFFu;
    };
    const auto is_white = [&](const ImDrawVert &v) {
        return std::abs(v.uv.x - white.x) < 1e-6f && std::abs(v.uv.y - white.y) < 1e-6f;
    };
    ImVec2 lo(1e9f, 1e9f);
    ImVec2 hi(-1e9f, -1e9f);
    for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        for (const ImDrawVert &v : w->DrawList->VtxBuffer) {
            if (is_white(v) && rgb(v.col) == rgb(header)) {
                lo = ImVec2(std::min(lo.x, v.pos.x), std::min(lo.y, v.pos.y));
                hi = ImVec2(std::max(hi.x, v.pos.x), std::max(hi.y, v.pos.y));
            }
        }
    }
    RV_EDITOR_CHECK(hi.x > lo.x && hi.y > lo.y); // the highlight of the picked row is drawn
    int same = 0;
    for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        for (const ImDrawVert &v : w->DrawList->VtxBuffer) {
            const bool on_row = v.pos.x >= lo.x && v.pos.x <= hi.x && v.pos.y >= lo.y && v.pos.y <= hi.y;
            same += on_row && !is_white(v) && rgb(v.col) == rgb(header) ? 1 : 0;
        }
    }
    std::fprintf(stderr, "  files: %d glyph corners on the picked row have the colour of its highlight\n", same);
    RV_EDITOR_CHECK(same == 0);
}

// R2: in a tile as narrow as the catalog's, every title of the old sections is an item as wide as its whole text (no "..."),
// and the page scrolls sideways by that much, so the title can be reached whole.
RV_EDITOR_TEST(rv_editor_test_s22_1_catalog_section_titles_are_drawn_whole_in_a_narrow_tile)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    rv_editor_theme_apply(stable_tiles::theme, ImGui::GetStyle());
    const char *const ids[] = { "catalog.section_1_pane_headers",
        "catalog.section_2_buttons",
        "catalog.section_3_text_fields",
        "catalog.section_4_checkbox_radio",
        "catalog.section_5_tree_list",
        "catalog.section_6_scrollbars_tabs",
        "catalog.section_7_transport",
        "catalog.section_8_log",
        "catalog.section_9_code",
        "catalog.section_10_game_frame",
        "catalog.section_11_icons_catalog",
        "catalog.section_12_palette_fonts" };
    rv_editor_probe_record record;
    float content = 0.0f;  // the page window's content width
    float widths[12] = {}; // the titles' widths whole, measured as the page measures them
    float line = 0.0f;
    for (int i = 0; i < 3; ++i) {
        record = rv_editor_probe_draw(
            gui,
            335.0f,
            [&](rv_editor_probe_record &) {
                for (int k = 0; k < 12; ++k) {
                    widths[k] = ImGui::CalcTextSize(rv_editor_text(ids[k])).x;
                }
                line = ImGui::GetFontSize();
                rv_editor_catalog_draw(stable_tiles::theme);
            },
            20000.0f);
        for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
            if (w->Active && w->ParentWindow != nullptr &&
                std::string(w->ParentWindow->Name) == "test") { // the page's scroll window
                content = std::max(content, w->ContentSize.x);
            }
        }
    }
    // Each title is laid out by its whole text (ItemSize), so the page scrolls sideways by the widest one; ImGui's own
    // section title is as wide as the area and would leave the page as narrow as the tile.
    float widest = 0.0f;
    for (const float w : widths) {
        widest = std::max(widest, w);
    }
    std::fprintf(stderr, "  catalog: widest title %.1f px, the page's content %.1f px wide\n", widest, content);
    RV_EDITOR_CHECK(content >= widest - edge_slack);
    // A title alone in a window narrower than it: its item is as wide as the text, so the window scrolls by that much.
    const char *const longest = rv_editor_text(ids[11]);
    ImGuiWindow *alone = nullptr;
    for (int i = 0; i < 3; ++i) {
        gui.frame(
            [&] {
                (void)rv_editor_scroll_begin("##alone", ImVec2(0.0f, 0.0f), true);
                alone = ImGui::GetCurrentWindow();
                rv_editor_section(longest);
                rv_editor_scroll_end(stable_tiles::theme);
            },
            200.0f,
            200.0f);
    }
    RV_EDITOR_CHECK(alone != nullptr);
    if (alone != nullptr) {
        std::fprintf(stderr,
            "  catalog: alone, a title of %.1f px in 200 px: content %.1f px wide\n",
            widths[11],
            alone->ContentSize.x);
        RV_EDITOR_CHECK(alone->ContentSize.x >= widths[11] - edge_slack);
    }
}

namespace
{

// `frames` frames of the scroll window of the `columns`-column table that `body` draws, each one scrolled to the far right
// after it was drawn; the ScrollMax.x of every frame.
template <class F>
std::vector<float> scroll_max_by_frame(rv_editor_test_imgui &gui, int columns, F body, int frames)
{
    std::vector<float> maxes;
    for (int i = 0; i < frames; ++i) {
        gui.frame(body, 200.0f, 300.0f);
        const ImGuiTable *t = drawn_table(columns);
        if (t == nullptr) {
            break;
        }
        ImGuiWindow *w = t->OuterWindow;
        maxes.push_back(w->ScrollMax.x);
        ImGui::SetScrollX(w, w->ScrollMax.x);
    }
    return maxes;
}

// "" when the sizes settle within a couple of frames and then stay within a pixel, else what happened.
std::string scroll_growth(const std::vector<float> &maxes, size_t frames)
{
    if (maxes.size() < frames) {
        return "the table was not drawn in every frame";
    }
    float lo = 1e9f;
    float hi = -1e9f;
    for (size_t i = 3; i < maxes.size(); ++i) {
        lo = std::min(lo, maxes[i]);
        hi = std::max(hi, maxes[i]);
    }
    if (hi <= 0.0f) {
        return "the table does not scroll sideways at all: the test does not hold";
    }
    if (hi - lo > 1.0f) {
        return "ScrollMaxX grows from " + std::to_string(maxes[3]) + " to " + std::to_string(maxes.back());
    }
    return "";
}

} // namespace

// R6: a row that spans the columns of a fixed-fit table, with the scroll kept at its far right, does not widen the table
// frame after frame: the width of the row (the visible width plus the scroll) is not taken for the width of column 0.
RV_EDITOR_TEST(rv_editor_test_s22_1_spanning_row_does_not_grow_the_scroll_width)
{
    rv_editor_test_imgui gui(true);
    rv_editor_theme_apply(stable_tiles::theme, ImGui::GetStyle());
    const rv_editor_table_column columns[3] = { { "A" }, { "B" }, { "C" } };
    const rv_editor_table_style style = { .header = true, .sizing = rv_editor_table_sizing::fixed_fit };
    const auto body = [&] {
        (void)rv_editor_content_begin("##c", ImVec2(180.0f, 200.0f), stable_tiles::theme);
        if (rv_editor_table_begin("##t", columns, 3, style)) {
            for (int r = 0; r < 3; ++r) {
                ImGui::PushID(r);
                rv_editor_table_row();
                rv_editor_table_cell();
                (void)rv_editor_list_row("##row",
                    "a row label that is longer than the whole area is wide",
                    stable_tiles::theme,
                    { .span_columns = true });
                rv_editor_table_cell();
                ImGui::TextUnformatted("b");
                rv_editor_table_cell();
                ImGui::TextUnformatted("c");
                ImGui::PopID();
            }
            rv_editor_table_end();
        }
        rv_editor_content_end(stable_tiles::theme);
    };
    const std::vector<float> maxes = scroll_max_by_frame(gui, 3, body, 40);
    const std::string bad = scroll_growth(maxes, 40);
    report("spanning row", bad);
    RV_EDITOR_CHECK(bad.empty());
}

// R6, Problems: one long problem, the pane scrolled to its far right in every frame, keeps the scroll width.
RV_EDITOR_TEST(rv_editor_test_s22_1_problems_scroll_width_does_not_grow)
{
    s13::load_texts();
    rv_editor_test_imgui gui(true);
    rv_editor_theme_apply(stable_tiles::theme, ImGui::GetStyle());
    s13::fixture f(gui, rv_editor_pane_kind::inspector_report);
    f.states[1]();
    f.app().problems.push_back({ f.where / "a_rather_long_file_name_to_scroll_sideways.cpp",
        3,
        5,
        true,
        "expected ';' after the declaration of a member with a very long name",
        "build" });
    const std::vector<float> maxes = scroll_max_by_frame(
        gui,
        5,
        [&] {
            rv_editor_shell_pane(&f.shell, s13::pane, f.kind, stable_tiles::theme);
        },
        40);
    const std::string bad = scroll_growth(maxes, 40);
    report("problems", bad);
    RV_EDITOR_CHECK(bad.empty());
}
