#include "font/rv_editor_font.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace rv_editor
{

namespace
{

ImFont *rv_editor_ui = nullptr;
ImFont *rv_editor_ui_italic = nullptr;
ImFont *rv_editor_code_small = nullptr;
ImFont *rv_editor_code_vga = nullptr;
float rv_editor_font_scale = 1.0f;
rv_editor_code_size rv_editor_code_current = rv_editor_code_size::normal;

// PxPlus IBM VGA 9x16 and IBM EGA 8x14: pixel outlines on a 16 and a 14 px em.
constexpr int rv_editor_code_vga_height = 16;
constexpr int rv_editor_code_ega_height = 14;

// The code fonts are pixel outlines on whole units, so at a whole multiple of
// their cell every edge lands on a pixel boundary and nothing is smoothed.
ImFontConfig rv_editor_font_config(const char *name)
{
    ImFontConfig config;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    std::strncpy(config.Name, name, sizeof(config.Name) - 1);
    return config;
}

// The interface font is an outline font: smoothed, on whole pixels vertically.
ImFontConfig rv_editor_font_config_ui(const char *name)
{
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    std::strncpy(config.Name, name, sizeof(config.Name) - 1);
    return config;
}

} // namespace

bool rv_editor_fonts_add(ImFontAtlas &atlas, float scale)
{
    const float k = std::max(1.0f, scale);
    rv_editor_font_scale = k;
    ImFontConfig ui = rv_editor_font_config_ui("Liberation Sans");
    rv_editor_ui = atlas.AddFontFromFileTTF(RV_EDITOR_UI_FONT_FILE, rv_editor_font_ui_px * k, &ui);
    ImFontConfig italic = rv_editor_font_config_ui("Liberation Sans Italic");
    rv_editor_ui_italic = atlas.AddFontFromFileTTF(RV_EDITOR_UI_ITALIC_FONT_FILE, rv_editor_font_ui_px * k, &italic);
    if (rv_editor_ui == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: %s does not load\n", RV_EDITOR_UI_FONT_FILE);
    }
    if (rv_editor_ui_italic == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: %s does not load; titles draw upright\n", RV_EDITOR_UI_ITALIC_FONT_FILE);
        rv_editor_ui_italic = rv_editor_ui;
    }

    ImFontConfig ega = rv_editor_font_config("PxPlus IBM EGA 8x14");
    rv_editor_code_small = atlas.AddFontFromFileTTF(RV_EDITOR_CODE_FONT_SMALL_FILE,
        static_cast<float>(rv_editor_code_ega_height) * k, &ega);
    ImFontConfig vga = rv_editor_font_config("PxPlus IBM VGA 9x16");
    rv_editor_code_vga =
        atlas.AddFontFromFileTTF(RV_EDITOR_CODE_FONT_FILE, static_cast<float>(rv_editor_code_vga_height) * k, &vga);
    if (rv_editor_code_small == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: %s does not load; the small code size draws in 9x16\n",
            RV_EDITOR_CODE_FONT_SMALL_FILE);
    }
    if (rv_editor_code_vga == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: %s does not load; code draws in 8x14\n", RV_EDITOR_CODE_FONT_FILE);
    }
    rv_editor_font_code_size_set(rv_editor_code_current);
    return rv_editor_ui != nullptr && (rv_editor_code_small != nullptr || rv_editor_code_vga != nullptr);
}

void rv_editor_font_scale_set(float scale)
{
    rv_editor_font_scale = std::max(1.0f, scale);
}

ImFont *rv_editor_font_ui()
{
    return rv_editor_ui;
}

ImFont *rv_editor_font_ui_italic()
{
    return rv_editor_ui_italic;
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
        case rv_editor_code_size::small: return "small";
        case rv_editor_code_size::normal: return "normal";
        case rv_editor_code_size::large: return "large";
    }
    return "normal";
}

bool rv_editor_code_size_parse(const char *name, rv_editor_code_size &size)
{
    for (const rv_editor_code_size s : { rv_editor_code_size::small, rv_editor_code_size::normal,
             rv_editor_code_size::large }) {
        if (std::strcmp(name, rv_editor_code_size_name(s)) == 0) {
            size = s;
            return true;
        }
    }
    return false;
}

void rv_editor_font_code_push()
{
    const float k = rv_editor_font_scale;
    switch (rv_editor_code_current) {
        case rv_editor_code_size::small:
            ImGui::PushFont(rv_editor_code_small, rv_editor_code_ega_height * k);
            return;
        case rv_editor_code_size::normal:
            ImGui::PushFont(rv_editor_code_vga, rv_editor_code_vga_height * k);
            return;
        case rv_editor_code_size::large:
            ImGui::PushFont(rv_editor_code_vga, 2.0f * rv_editor_code_vga_height * k);
            return;
    }
}

void rv_editor_font_code_pop()
{
    ImGui::PopFont();
}

} // namespace rv_editor
