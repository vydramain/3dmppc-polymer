// S11, R16, AC17: Assets lists its files only in a table with the columns Name, Type, Size and Folder; there is no
// Details check box, no `pane_assets.details` text and no icon grid at any width; a click on a row selects the asset.

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

constexpr float roomy_w = 900.0f;
constexpr float roomy_h = 700.0f;
constexpr size_t table_state = 3; // five files, no filter
constexpr size_t sound_row = 4;   // sfx/beep.wav, the fifth in the order Assets sorts them
constexpr const char *column_keys[] = { "pane_assets.table_name",
    "pane_assets.table_type",
    "pane_assets.table_size",
    "pane_assets.table_folder" };

// The names of the columns, left to right, of the table the last frame drew (the pane has one table).
std::vector<std::string> drawn_columns()
{
    std::vector<std::string> names;
    ImGuiContext &g = *ImGui::GetCurrentContext();
    for (int n = 0; n < g.Tables.GetMapSize(); ++n) {
        const ImGuiTable *table = g.Tables.TryGetMapData(n);
        if (table == nullptr || table->LastFrameActive < g.FrameCount - 1) {
            continue;
        }
        for (int order = 0; order < table->ColumnsCount; ++order) {
            for (int c = 0; c < table->ColumnsCount; ++c) {
                if (table->Columns[c].DisplayOrder == order) {
                    names.emplace_back(ImGui::TableGetColumnName(table, c));
                }
            }
        }
    }
    return names;
}

} // namespace

// The four column texts live in the texts file under their own keys (an unknown key answers with itself).
RV_EDITOR_TEST(rv_editor_test_assets_table_column_texts_are_in_the_texts_file)
{
    s15::load_texts();
    for (const char *key : column_keys) {
        RV_EDITOR_CHECK(std::string(rv_editor_text(key)) != key);
    }
}

// R16: the columns are Name, Type, Size, Folder, in this order, with the texts of the texts file.
RV_EDITOR_TEST(rv_editor_test_assets_table_has_the_columns_name_type_size_folder_in_order)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    f.states[table_state]();
    f.settled(roomy_w, roomy_h);
    const std::vector<std::string> expected{ rv_editor_text(column_keys[0]),
        rv_editor_text(column_keys[1]),
        rv_editor_text(column_keys[2]),
        rv_editor_text(column_keys[3]) };
    const std::vector<std::string> got = drawn_columns();
    std::string joined;
    for (const std::string &s : got) {
        joined += "[" + s + "]";
    }
    std::fprintf(stderr, "  assets columns: %s\n", joined.c_str());
    RV_EDITOR_CHECK(got == expected);
}

// R16: no Details check box in any state at any tile size, no text for it, and no icon cell at any width.
RV_EDITOR_TEST(rv_editor_test_assets_has_no_details_checkbox_and_no_icon_grid_at_any_width)
{
    s15::load_texts();
    RV_EDITOR_CHECK(std::string(rv_editor_text("pane_assets.details")) == "pane_assets.details");
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    std::vector<ImVec2> sizes = s15::tile_sizes(gui, f.kind, ImVec2(roomy_w, roomy_h));
    sizes.emplace_back(300.0f, roomy_h);
    sizes.emplace_back(1600.0f, roomy_h);
    int details = 0;
    int cells = 0;
    for (const ImVec2 size : sizes) {
        for (size_t i = 0; i < f.states.size(); ++i) {
            const auto r = s15::snap(f, i, size.x, size.y);
            for (const auto &e : r.entries) {
                details += s15::text_of(e) == "Details" ? 1 : 0;
                cells += e.label.starts_with("##cell") ? 1 : 0;
            }
        }
    }
    std::fprintf(stderr, "  Details items: %d, icon cells: %d\n", details, cells);
    RV_EDITOR_CHECK(details == 0);
    RV_EDITOR_CHECK(cells == 0);
}

// R16: a project with files shows a row per file, in every tile size, and the table is the only list.
RV_EDITOR_TEST(rv_editor_test_assets_shows_one_row_per_file_at_every_width)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    for (const ImVec2 size : s15::tile_sizes(gui, f.kind, ImVec2(roomy_w, roomy_h))) {
        const auto r = s15::snap(f, table_state, size.x, size.y);
        int rows = 0;
        for (const auto &e : r.entries) {
            rows += e.label.ends_with("###row") ? 1 : 0;
        }
        std::fprintf(stderr, "  tile %.0fx%.0f: %d rows\n", size.x, size.y, rows);
        RV_EDITOR_CHECK(rows == 5);
    }
}

// AC17: a click on a row selects the asset as a click on a grid cell did: the preview of a sound shows Play and Stop,
// where before the click it shows neither.
RV_EDITOR_TEST(rv_editor_test_assets_click_on_a_row_selects_the_asset)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    f.states[table_state]();
    const std::string play = rv_editor_text("asset_preview.button_play");
    const std::string stop = rv_editor_text("asset_preview.button_stop");
    const auto before = f.settled(roomy_w, roomy_h);
    RV_EDITOR_CHECK(s15::find_text(before, play) == nullptr && s15::find_text(before, stop) == nullptr);
    std::vector<const rv_editor_probe_entry *> rows;
    for (const auto &e : before.entries) {
        if (e.label.ends_with("###row")) {
            rows.push_back(&e);
        }
    }
    RV_EDITOR_CHECK(rows.size() == 5);
    if (rows.size() != 5) {
        return;
    }
    RV_EDITOR_CHECK(rows[sound_row]->label.starts_with("sfx/beep.wav"));
    f.click(*rows[sound_row], roomy_w, roomy_h);
    const auto after = f.settled(roomy_w, roomy_h);
    RV_EDITOR_CHECK(s15::find_text(after, play) != nullptr);
    RV_EDITOR_CHECK(s15::find_text(after, stop) != nullptr);
}
