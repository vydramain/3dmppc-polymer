// S16: the Type column of Assets tells the kind of a file by a coloured square (the kind's colour, one text line high) and
// the kind's word from the texts, not by a two-letter code (Im, Sn, Fi); the column holds the longest word whole.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr float tile_w = 469.0f; // the Assets tile of the Scene layout in a 1280 px window
constexpr float tile_h = 330.0f;
constexpr size_t table_state = 3;
constexpr int columns = 4;
constexpr float size_slack = 2.5f; // the square is "about" a text line high

const char *const kind_keys[] = { "pane_assets.kind_image",
    "pane_assets.kind_sound",
    "pane_assets.kind_model",
    "pane_assets.kind_scene",
    "pane_assets.kind_code",
    "pane_assets.kind_header",
    "pane_assets.kind_script",
    "pane_assets.kind_manifest",
    "pane_assets.kind_text",
    "pane_assets.kind_folder",
    "pane_assets.kind_link",
    "pane_assets.kind_other" };

struct kind_file {
    const char *file;
    const char *key;
};
const kind_file kind_files[] = { { "pic.png", "pane_assets.kind_image" },
    { "clip.wav", "pane_assets.kind_sound" },
    { "ship.gltf", "pane_assets.kind_model" },
    { "level.scene.toml", "pane_assets.kind_scene" },
    { "main.cpp", "pane_assets.kind_code" } };

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

// The Assets table of a project of the five files of kind_files, drawn settled.
rv_editor_probe_record draw_kinds(s15::fixture &f)
{
    const std::filesystem::path root = f.where / "kinds_project";
    std::filesystem::create_directories(root / "res");
    for (const kind_file &k : kind_files) {
        std::ofstream(root / "res" / k.file) << "x";
    }
    f.states[table_state]();
    f.open_project(root); // another root: Assets reads it at once, not after its refresh period
    return f.settled(tile_w, tile_h);
}

// True when the draw lists hold a filled rectangle of `color` (a vertex on the white pixel, which glyphs never use).
bool filled_with(uint32_t color)
{
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    for (const ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        for (const ImDrawVert &v : w->DrawList->VtxBuffer) {
            if (v.col == rv_editor_col(color) && v.uv.x == white.x && v.uv.y == white.y) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

// The kind words are texts: one key a kind, each with a word of its own.
RV_EDITOR_TEST(rv_editor_test_s16_every_kind_has_a_word_in_the_texts)
{
    s15::load_texts();
    for (const char *key : kind_keys) {
        const std::string word = rv_editor_text(key);
        RV_EDITOR_CHECK(word != key && !word.empty());
    }
}

// The kind of a path is the word of its kind.
RV_EDITOR_TEST(rv_editor_test_s16_file_kind_word_by_path)
{
    s15::load_texts();
    const struct {
        const char *path;
        const char *key;
    } cases[] = { { "a/pic.png", "pane_assets.kind_image" },
        { "a/clip.wav", "pane_assets.kind_sound" },
        { "a/clip.pcm", "pane_assets.kind_sound" },
        { "a/ship.gltf", "pane_assets.kind_model" },
        { "a/level.scene.toml", "pane_assets.kind_scene" },
        { "a/main.cpp", "pane_assets.kind_code" },
        { "a/main.h", "pane_assets.kind_header" },
        { "a/main.hpp", "pane_assets.kind_header" },
        { "a/boot.lua", "pane_assets.kind_script" },
        { "a/disc.toml", "pane_assets.kind_manifest" },
        { "a/notes.txt", "pane_assets.kind_text" },
        { "a/notes.md", "pane_assets.kind_text" },
        { "a/thing.xyz", "pane_assets.kind_other" } };
    for (const auto &c : cases) {
        const std::string got = rv_editor_file_kind_word(c.path);
        std::fprintf(stderr, "  %s: '%s', want '%s'\n", c.path, got.c_str(), rv_editor_text(c.key));
        RV_EDITOR_CHECK(got == rv_editor_text(c.key));
    }
}

// Each row: the kind's word, and left of it a square about a text line high; no two-letter code is the cell's text.
RV_EDITOR_TEST(rv_editor_test_s16_assets_type_cell_is_a_square_and_the_kind_word)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    const auto record = draw_kinds(f);
    for (const auto &e : record.entries) {
        const std::string text = s15::text_of(e);
        RV_EDITOR_CHECK(text != "Im" && text != "Sn" && text != "Fi" && text != "Tm");
    }
    for (const kind_file &k : kind_files) {
        const auto *row = s15::find_text(record, std::string("res/") + k.file);
        RV_EDITOR_CHECK(row != nullptr);
        if (row == nullptr) {
            continue;
        }
        const float mid = (row->y0 + row->y1) / 2.0f;
        const rv_editor_probe_entry *word = nullptr;
        for (const auto &e : record.entries) {
            if (s15::text_of(e) == rv_editor_text(k.key) && e.y0 <= mid && mid <= e.y1) {
                word = &e;
            }
        }
        std::fprintf(stderr, "  %s: word '%s' %s\n", k.file, rv_editor_text(k.key), word != nullptr ? "found" : "missing");
        RV_EDITOR_CHECK(word != nullptr);
        if (word == nullptr) {
            continue;
        }
        const float line = ImGui::GetTextLineHeight();
        bool square = false;
        for (const auto &e : record.entries) {
            const bool same_row = e.y0 <= mid && mid <= e.y1 && e.x0 >= row->x1 && e.x1 <= word->x0 + 0.5f;
            square = square ||
                (same_row && std::abs((e.x1 - e.x0) - line) <= size_slack && std::abs((e.y1 - e.y0) - line) <= size_slack);
        }
        std::fprintf(stderr, "  %s: square left of the word %s\n", k.file, square ? "found" : "missing");
        RV_EDITOR_CHECK(square);
    }
}

// The square is filled with the kind's colour (Image, Sound and Code as rv_editor_file_chip says).
RV_EDITOR_TEST(rv_editor_test_s16_assets_type_square_has_the_kind_colour)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    (void)draw_kinds(f);
    for (const char *file : { "pic.png", "clip.wav", "main.cpp" }) {
        const char *code = nullptr;
        uint32_t color = 0;
        rv_editor_file_chip(file, code, color);
        std::fprintf(stderr,
            "  %s: filled rectangle of colour %06x %s\n",
            file,
            color,
            filled_with(color) ? "found" : "missing");
        RV_EDITOR_CHECK(filled_with(color));
    }
}

// The Type column holds a square and the longest kind word whole, and its heading.
RV_EDITOR_TEST(rv_editor_test_s16_assets_type_column_holds_the_longest_word)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    s15::fixture f(gui, rv_editor_pane_kind::assets);
    (void)draw_kinds(f);
    const ImGuiTable *t = drawn_table();
    RV_EDITOR_CHECK(t != nullptr);
    if (t == nullptr) {
        return;
    }
    float widest = 0.0f;
    for (const char *key : kind_keys) {
        widest = std::max(widest, ImGui::CalcTextSize(rv_editor_text(key)).x);
    }
    float heading = 0.0f;
    float shown = 0.0f;
    for (int c = 0; c < t->ColumnsCount; ++c) {
        if (std::string(ImGui::TableGetColumnName(t, c)) == rv_editor_text("pane_assets.table_type")) {
            shown = t->Columns[c].WorkMaxX - t->Columns[c].WorkMinX;
            heading = ImGui::CalcTextSize(rv_editor_text("pane_assets.table_type")).x;
        }
    }
    const float need = std::max(heading, ImGui::GetTextLineHeight() + widest);
    std::fprintf(stderr, "  Type column %.0f px, square and the longest word %.0f px\n", shown, need);
    RV_EDITOR_CHECK(shown + 0.5f >= need);
}
