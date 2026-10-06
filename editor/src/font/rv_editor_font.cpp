#include "font/rv_editor_font.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <string_view>

#include "pdk/rv_err.h"

#include "font/rv_editor_font_ttf.hpp"

namespace rv_editor
{

namespace
{

ImFont *rv_editor_ui = nullptr;
ImFont *rv_editor_code_small = nullptr;
ImFont *rv_editor_code_vga = nullptr;
float rv_editor_font_scale = 1.0f;
rv_editor_code_size rv_editor_code_current = rv_editor_code_size::small;

// PxPlus IBM VGA 9x16 and IBM EGA 8x14: pixel outlines on a 16 and a 14 px em.
constexpr int rv_editor_code_vga_height = 16;
constexpr int rv_editor_code_ega_height = 14;

// Rounding adjustment for font size calculation to nearest pixel
constexpr float font_size_rounding_adjustment = 0.5f;

// Grid key multiplier to preserve fractional scale precision
constexpr double grid_key_scale_multiplier = 1000.0;

// Rounding adjustment for grid key fractional part
constexpr double grid_key_rounding_adjustment = 0.5;

// UI font configuration name (pdklib 5x7 font)
constexpr std::string_view ui_font_config_name = "rv_font 5x7";

// EGA font configuration name (PxPlus IBM EGA 8x14)
constexpr std::string_view ega_font_config_name = "PxPlus IBM EGA 8x14";

// VGA font configuration name (PxPlus IBM VGA 9x16)
constexpr std::string_view vga_font_config_name = "PxPlus IBM VGA 9x16";

// Code size names as stored in view preferences file
constexpr std::string_view code_size_name_small = "small";
constexpr std::string_view code_size_name_normal = "normal";
constexpr std::string_view code_size_name_large = "large";

// Multiplier for large code size relative to VGA normal size
constexpr int large_code_size_multiplier = 2;

// Every font here is pixel outlines on whole units, so at a whole multiple of its
// cell every edge lands on a pixel boundary and nothing is smoothed.
float rv_editor_font_size(int base, float scale)
{
    return std::floor(static_cast<float>(base) * scale + font_size_rounding_adjustment);
}

ImFontConfig rv_editor_font_config(const char *name)
{
    ImFontConfig config;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    std::strncpy(config.Name, name, sizeof(config.Name) - 1);
    return config;
}

} // namespace

int rv_editor_fonts_add(ImFontAtlas &atlas, float scale)
{
    ImFont *const old_ui = rv_editor_ui;
    ImFont *const old_small = rv_editor_code_small;
    ImFont *const old_vga = rv_editor_code_vga;
    const float old_scale = rv_editor_font_scale;
    const float k = std::max(1.0f, scale);
    rv_editor_font_scale = k;
    // Whole scales load the 1:1 grid at a multiple of its size; a fraction needs its own scaled grid.
    // The atlas keeps a pointer to the bytes for as long as it lives, so they are kept per grid.
    const bool whole = k == std::floor(k);
    const double grid = whole ? 1.0 : static_cast<double>(k);
    const int grid_key = static_cast<int>(std::floor(grid * grid_key_scale_multiplier +
        grid_key_rounding_adjustment));
    static std::map<int, std::string> ui_ttfs;
    if (!ui_ttfs.contains(grid_key)) {
        ui_ttfs.emplace(grid_key, rv_editor_font_ttf(grid));
    }
    const std::string &ui_ttf = ui_ttfs.at(grid_key);
    float ui_size = static_cast<float>(rv_editor_font_ui_height) * k;
    if (!whole) {
        ui_size = static_cast<float>(rv_editor_font_ttf_em(grid));
    }
    ImFontConfig ui = rv_editor_font_config(ui_font_config_name.data());
    ui.FontDataOwnedByAtlas = false;
    rv_editor_ui = atlas.AddFontFromMemoryTTF(const_cast<char *>(ui_ttf.data()), static_cast<int>(ui_ttf.size()),
        ui_size, &ui);

    ImFontConfig ega = rv_editor_font_config(ega_font_config_name.data());
    rv_editor_code_small = atlas.AddFontFromFileTTF(RV_EDITOR_CODE_FONT_SMALL_FILE,
        rv_editor_font_size(rv_editor_code_ega_height, k), &ega);
    ImFontConfig vga = rv_editor_font_config(vga_font_config_name.data());
    rv_editor_code_vga =
        atlas.AddFontFromFileTTF(RV_EDITOR_CODE_FONT_FILE, rv_editor_font_size(rv_editor_code_vga_height, k), &vga);
    if (rv_editor_code_small == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: %s does not load; the small code size draws in 9x16\n",
            RV_EDITOR_CODE_FONT_SMALL_FILE);
    }
    if (rv_editor_code_vga == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: %s does not load; code draws in 8x14\n", RV_EDITOR_CODE_FONT_FILE);
    }
    const bool ok = rv_editor_ui != nullptr && (rv_editor_code_small != nullptr || rv_editor_code_vga != nullptr);
    // The previous set stays in the atlas until the new one is known good; a failure drops the new one.
    for (ImFont *font : { ok ? old_ui : rv_editor_ui, ok ? old_small : rv_editor_code_small,
             ok ? old_vga : rv_editor_code_vga }) {
        if (font != nullptr) {
            atlas.RemoveFont(font);
        }
    }
    if (!ok) {
        rv_editor_ui = old_ui;
        rv_editor_code_small = old_small;
        rv_editor_code_vga = old_vga;
        rv_editor_font_scale = old_scale;
        return RV_ERR_NOENT;
    }
    rv_editor_font_code_size_set(rv_editor_code_current);
    return RV_OK;
}

void rv_editor_font_scale_set(float scale)
{
    rv_editor_font_scale = std::max(1.0f, scale);
}

ImFont *rv_editor_font_ui()
{
    return rv_editor_ui;
}

void rv_editor_font_code_size_set(rv_editor_code_size size)
{
    if (rv_editor_code_vga == nullptr) {
        size = rv_editor_code_size::small;
    } else if (rv_editor_code_small == nullptr && size == rv_editor_code_size::small) {
        size = rv_editor_code_size::normal;
    }
    rv_editor_code_current = size;
}

rv_editor_code_size rv_editor_font_code_size()
{
    return rv_editor_code_current;
}

const char *rv_editor_code_size_name(rv_editor_code_size size)
{
    switch (size) {
    case rv_editor_code_size::small:
        return code_size_name_small.data();
    case rv_editor_code_size::normal:
        return code_size_name_normal.data();
    case rv_editor_code_size::large:
        return code_size_name_large.data();
    }
    return code_size_name_normal.data();
}

int rv_editor_code_size_parse(const char *name, rv_editor_code_size &size)
{
    for (const rv_editor_code_size s : { rv_editor_code_size::small, rv_editor_code_size::normal,
             rv_editor_code_size::large }) {
        if (std::strcmp(name, rv_editor_code_size_name(s)) == 0) {
            size = s;
            return RV_OK;
        }
    }
    return RV_ERR_INVAL;
}

void rv_editor_font_code_push()
{
    const float k = rv_editor_font_scale;
    switch (rv_editor_code_current) {
        case rv_editor_code_size::small:
            ImGui::PushFont(rv_editor_code_small, rv_editor_font_size(rv_editor_code_ega_height, k));
            return;
        case rv_editor_code_size::normal:
            ImGui::PushFont(rv_editor_code_vga, rv_editor_font_size(rv_editor_code_vga_height, k));
            return;
        case rv_editor_code_size::large:
            ImGui::PushFont(rv_editor_code_vga,
                rv_editor_font_size(large_code_size_multiplier * rv_editor_code_vga_height, k));
            return;
    }
}

void rv_editor_font_code_pop()
{
    ImGui::PopFont();
}

} // namespace rv_editor
