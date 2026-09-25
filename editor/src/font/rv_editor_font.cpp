#include "font/rv_editor_font.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include "font/rv_editor_font_ttf.hpp"

namespace rv_editor
{

namespace
{

ImFont *rv_editor_ui = nullptr;
ImFont *rv_editor_code = nullptr;

// Both fonts are pixel outlines on whole units, so at a whole multiple of their
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
    // The atlas keeps a pointer to the bytes for as long as it lives.
    static const std::string ui_ttf = rv_editor_font_ttf();
    ImFontConfig ui = rv_editor_font_config("rv_font 5x7");
    ui.FontDataOwnedByAtlas = false;
    rv_editor_ui = atlas.AddFontFromMemoryTTF(const_cast<char *>(ui_ttf.data()), static_cast<int>(ui_ttf.size()),
        static_cast<float>(rv_editor_font_ui_height * k), &ui);

    static const std::string code_ttf = rv_editor_font_code_ttf();
    ImFontConfig code = rv_editor_font_config("rv_editor code 6x11");
    code.FontDataOwnedByAtlas = false;
    rv_editor_code = atlas.AddFontFromMemoryTTF(const_cast<char *>(code_ttf.data()), static_cast<int>(code_ttf.size()),
        static_cast<float>(rv_editor_font_code_height * k), &code);
    return rv_editor_ui != nullptr && rv_editor_code != nullptr;
}

ImFont *rv_editor_font_ui()
{
    return rv_editor_ui;
}

void rv_editor_font_code_push()
{
    ImGui::PushFont(rv_editor_code, rv_editor_code->LegacySize);
}

void rv_editor_font_code_pop()
{
    ImGui::PopFont();
}

} // namespace rv_editor
