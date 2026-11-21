// S14.5 (live run 2): with the columns narrower than the tile, the selected Assets row was filled only to the end of the
// Folder column. The last column takes the rest of the table, so the highlight spans the whole row; the area does not grow
// sideways from frame to frame.

#include <algorithm>
#include <cstdio>
#include <cstring>
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

constexpr size_t selected_hero_state = 5; // gfx/hero.png selected
constexpr size_t table_state = 3;         // five files, one name 100 words long
constexpr int columns = 4;
constexpr int frames = 40;

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

// The columns are narrower than the tile: they add up to the table's width, the Folder column ends at its right edge and
// nothing scrolls sideways, so the selected row is lit across the whole table.
RV_EDITOR_TEST(rv_editor_test_s14_5_assets_columns_fill_a_wide_table)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    f.states[selected_hero_state]();
    // The filter hides the long name, so every column is narrow.
    std::strncpy(f.app().assets_ui.filter, "hero", sizeof(f.app().assets_ui.filter) - 1);
    constexpr float width = 900.0f;
    constexpr float height = 330.0f;
    f.settled(width, height);
    const ImGuiTable *t = drawn_table();
    RV_EDITOR_CHECK(t != nullptr);
    if (t == nullptr) {
        return;
    }
    float sum = 0.0f;
    float right = 0.0f;
    for (int c = 0; c < t->ColumnsCount; ++c) {
        sum += t->Columns[c].MaxX - t->Columns[c].MinX;
        right = std::max(right, t->Columns[c].MaxX);
    }
    const float inner = t->InnerRect.GetWidth();
    std::fprintf(stderr,
        "  columns %.1f px, table %.1f px, last column ends %.1f, inner edge %.1f, ScrollMax.x %.1f\n",
        sum,
        inner,
        right,
        t->InnerRect.Max.x,
        t->InnerWindow != nullptr ? t->InnerWindow->ScrollMax.x : -1.0f);
    RV_EDITOR_CHECK(sum > inner - 1.0f && sum < inner + 1.0f);
    RV_EDITOR_CHECK(right >= t->InnerRect.Max.x - 1.0f);
    RV_EDITOR_CHECK(t->InnerWindow != nullptr && t->InnerWindow->ScrollMax.x == 0.0f);
}

// A narrow tile with the long name: scrolling sideways is fine, but its extent does not grow frame after frame.
RV_EDITOR_TEST(rv_editor_test_s14_5_assets_sideways_scroll_does_not_grow)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    f.states[table_state]();
    constexpr float width = 300.0f;
    constexpr float height = 330.0f;
    f.settled(width, height);
    const ImGuiTable *t = drawn_table();
    RV_EDITOR_CHECK(t != nullptr);
    if (t == nullptr || t->InnerWindow == nullptr) {
        return;
    }
    const float first = t->InnerWindow->ScrollMax.x;
    float widest = first;
    for (int i = 0; i < frames; ++i) {
        f.draw(width, height);
        t = drawn_table();
        RV_EDITOR_CHECK(t != nullptr && t->InnerWindow != nullptr);
        if (t == nullptr || t->InnerWindow == nullptr) {
            return;
        }
        widest = std::max(widest, t->InnerWindow->ScrollMax.x);
    }
    std::fprintf(stderr, "  ScrollMax.x %.1f after settling, at most %.1f over %d frames\n", first, widest, frames);
    RV_EDITOR_CHECK(widest <= first + 0.5f);
}
