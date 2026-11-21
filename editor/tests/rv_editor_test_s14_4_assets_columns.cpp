// S14.4 (live run): in the Scene layout the Assets tile is about 469 px wide, and its table cut the Name column to the
// first letters of each name and the other columns to their narrowest. Text is whole (R2, rule 2): a column is as wide as
// its widest text and the area scrolls sideways when the columns are wider than the tile; nothing is cut or ellipsized.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr float scene_assets_w = 469.0f; // the Assets tile of the Scene layout in a 1280 px window
constexpr float scene_assets_h = 330.0f;
constexpr size_t table_state = 3; // five files, no filter; one name is 100 words long
constexpr int columns = 4;

const ImGuiTable *drawn_table()
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    for (int n = 0; n < g.Tables.GetMapSize(); ++n) {
        const ImGuiTable *t = g.Tables.TryGetMapData(n);
        if (t != nullptr && t->ColumnsCount == columns && t->LastFrameActive >= g.FrameCount - 1) {
            return t;
        }
    }
    return nullptr;
}

} // namespace

// R2: every file name is whole in the Name column: the column is at least as wide as the widest name.
RV_EDITOR_TEST(rv_editor_test_s14_4_assets_name_column_holds_the_whole_names)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    f.states[table_state]();
    const auto record = f.settled(scene_assets_w, scene_assets_h);
    const ImGuiTable *t = drawn_table();
    RV_EDITOR_CHECK(t != nullptr);
    if (t == nullptr) {
        return;
    }
    float widest = 0.0f;
    int rows = 0;
    for (const auto &e : record.entries) {
        if (e.label.ends_with("###row")) {
            widest = std::max(widest, e.text_w);
            ++rows;
        }
    }
    RV_EDITOR_CHECK(rows == 5);
    int name = 0;
    for (int c = 0; c < t->ColumnsCount; ++c) {
        name = t->Columns[c].DisplayOrder == 0 ? c : name;
    }
    const float shown = t->Columns[name].WorkMaxX - t->Columns[name].WorkMinX;
    std::fprintf(stderr, "  Name column %.0f px, the widest name %.0f px\n", shown, widest);
    RV_EDITOR_CHECK(shown + 0.5f >= widest);
}

// R2: every column heading is whole, and no text of the tile is cut with "...".
RV_EDITOR_TEST(rv_editor_test_s14_4_assets_headings_are_whole_and_nothing_is_ellipsized)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    f.states[table_state]();
    const auto record = f.settled(scene_assets_w, scene_assets_h);
    const ImGuiTable *t = drawn_table();
    RV_EDITOR_CHECK(t != nullptr);
    if (t == nullptr) {
        return;
    }
    for (int c = 0; c < t->ColumnsCount; ++c) {
        const char *heading = ImGui::TableGetColumnName(t, c);
        const float shown = t->Columns[c].WorkMaxX - t->Columns[c].WorkMinX;
        const float want = ImGui::CalcTextSize(heading).x;
        std::fprintf(stderr, "  column '%s': %.0f px, heading %.0f px\n", heading, shown, want);
        RV_EDITOR_CHECK(shown + 0.5f >= want);
    }
    for (const auto &e : record.entries) {
        RV_EDITOR_CHECK(e.label.substr(0, e.label.find("##")).find("...") == std::string::npos);
    }
}
