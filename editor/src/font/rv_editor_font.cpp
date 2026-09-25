#include "font/rv_editor_font.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "font/rv_editor_font_code_data.hpp"
#include "font/rv_editor_font_ttf.hpp"

namespace rv_editor
{

namespace
{

ImFont *rv_editor_ui = nullptr;
ImFont *rv_editor_code_small = nullptr;
ImFont *rv_editor_code_vga = nullptr;
int rv_editor_font_scale = 1;
rv_editor_code_size rv_editor_code_current = rv_editor_code_size::normal;

// PxPlus IBM VGA 9x16: pixel outlines on a 16 px em.
constexpr int rv_editor_code_vga_height = 16;

// Every font here is pixel outlines on whole units, so at a whole multiple of its
// cell every edge lands on a pixel boundary and nothing is smoothed.
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

bool rv_editor_fonts_add(ImFontAtlas &atlas, int scale)
{
    const int k = std::max(1, scale);
    rv_editor_font_scale = k;
    // The atlas keeps a pointer to the bytes for as long as it lives.
    static const std::string ui_ttf = rv_editor_font_ttf();
    ImFontConfig ui = rv_editor_font_config("rv_font 5x7");
    ui.FontDataOwnedByAtlas = false;
    rv_editor_ui = atlas.AddFontFromMemoryTTF(const_cast<char *>(ui_ttf.data()), static_cast<int>(ui_ttf.size()),
        static_cast<float>(rv_editor_font_ui_height * k), &ui);

    static const std::string code_ttf = rv_editor_font_code_ttf();
    ImFontConfig code = rv_editor_font_config("rv_editor code 6x11");
    code.FontDataOwnedByAtlas = false;
    rv_editor_code_small = atlas.AddFontFromMemoryTTF(const_cast<char *>(code_ttf.data()),
        static_cast<int>(code_ttf.size()), static_cast<float>(rv_editor_code_cell_height * k), &code);

    ImFontConfig vga = rv_editor_font_config("PxPlus IBM VGA 9x16");
    rv_editor_code_vga =
        atlas.AddFontFromFileTTF(RV_EDITOR_CODE_FONT_FILE, static_cast<float>(rv_editor_code_vga_height * k), &vga);
    if (rv_editor_code_vga == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: %s does not load; code draws in the 6x11 font\n", RV_EDITOR_CODE_FONT_FILE);
        rv_editor_code_current = rv_editor_code_size::small;
    }
    return rv_editor_ui != nullptr && rv_editor_code_small != nullptr;
}

ImFont *rv_editor_font_ui()
{
    return rv_editor_ui;
}

void rv_editor_font_code_size_set(rv_editor_code_size size)
{
    rv_editor_code_current = rv_editor_code_vga == nullptr ? rv_editor_code_size::small : size;
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
    const float k = static_cast<float>(rv_editor_font_scale);
    switch (rv_editor_code_current) {
        case rv_editor_code_size::small:
            ImGui::PushFont(rv_editor_code_small, rv_editor_code_cell_height * k);
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
