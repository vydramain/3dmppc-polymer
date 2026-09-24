#include "font/rv_editor_font.hpp"

#include <algorithm>
#include <cstring>

namespace rv_editor
{

ImFont *rv_editor_font_add(ImFontAtlas &atlas, int scale)
{
    // The outlines sit on a grid of 100 font units per font pixel, so at a whole
    // multiple of 16 px every edge lands on a pixel boundary: coverage is 0 or 255
    // and nothing is smoothed. Oversampling would only blur that.
    ImFontConfig config;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    std::strncpy(config.Name, "PxPlus IBM VGA 9x16", sizeof(config.Name) - 1);
    const float size = static_cast<float>(rv_editor_font_height * std::max(1, scale));
    return atlas.AddFontFromFileTTF(RV_EDITOR_FONT_PATH, size, &config);
}

} // namespace rv_editor
