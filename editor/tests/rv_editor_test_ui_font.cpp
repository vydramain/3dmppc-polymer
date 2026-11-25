#include <cmath>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "pdk/rv_err.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

std::vector<std::string> glyph_pixels(ImFontAtlas &atlas, ImWchar code)
{
    const ImFontGlyph *glyph = ImGui::GetFontBaked()->FindGlyphNoFallback(code);
    RV_EDITOR_CHECK(glyph != nullptr && glyph->Visible);
    if (glyph == nullptr || !glyph->Visible) {
        return {};
    }

    const ImTextureData &texture = *atlas.TexData;
    const int x0 = std::lround(glyph->U0 * texture.Width);
    const int x1 = std::lround(glyph->U1 * texture.Width);
    const int y0 = std::lround(glyph->V0 * texture.Height);
    const int y1 = std::lround(glyph->V1 * texture.Height);
    std::vector<std::string> rows;
    for (int y = y0; y < y1; ++y) {
        std::string row;
        for (int x = x0; x < x1; ++x) {
            const int pixel = (y * texture.Width + x) * texture.BytesPerPixel;
            const unsigned char alpha = texture.Pixels[pixel + texture.BytesPerPixel - 1];
            RV_EDITOR_CHECK(alpha == 0 || alpha == 255);
            row += alpha == 255 ? '#' : '.';
        }
        rows.push_back(row);
    }
    return rows;
}

std::vector<int> vertical_ink_runs(const std::vector<std::string> &rows)
{
    std::vector<int> runs;
    int thickness = 0;
    for (const std::string &row : rows) {
        if (!row.empty() && row[row.size() / 2] == '#') {
            ++thickness;
        } else if (thickness > 0) {
            runs.push_back(thickness);
            thickness = 0;
        }
    }
    if (thickness > 0) {
        runs.push_back(thickness);
    }
    return runs;
}

void check_fractional_font(ImFontAtlas &atlas)
{
    const auto e = glyph_pixels(atlas, 'E');
    // Inspect the middle column of E where all three horizontal bars cross it.
    const auto bars = vertical_ink_runs(e);
    RV_EDITOR_CHECK(bars.size() == 3 && bars[0] == bars[1] && bars[1] == bars[2]);

    const auto o = glyph_pixels(atlas, 'O');
    const std::string middle = o.empty() ? std::string{} : o[o.size() / 2];
    RV_EDITOR_CHECK(!middle.empty() && middle.front() == '#' && middle.back() == '#' && middle[middle.size() / 2] == '.');
    RV_EDITOR_CHECK(!glyph_pixels(atlas, U'Ж').empty());

    if (!e.empty() && !o.empty()) {
        const float e_advance = ImGui::CalcTextSize("E").x;
        const float o_advance = ImGui::CalcTextSize("O").x;
        RV_EDITOR_CHECK(e_advance > static_cast<float>(e.front().size()));
        RV_EDITOR_CHECK(o_advance > static_cast<float>(o.front().size()));
        RV_EDITOR_CHECK(ImGui::CalcTextSize("EO").x == e_advance + o_advance);
    }
}

void check_whole_scale_font(ImFontAtlas &atlas, int scale)
{
    // The original 5x7 E is the reference for whole scales.
    const std::vector<std::string> original_e = { "#####", "#....", "#....", "####.", "#....", "#....", "#####" };
    std::vector<std::string> expected;
    for (const std::string &row : original_e) {
        std::string scaled_row;
        for (const char pixel : row) {
            scaled_row.append(scale, pixel);
        }
        expected.insert(expected.end(), scale, scaled_row);
    }
    RV_EDITOR_CHECK(glyph_pixels(atlas, 'E') == expected);
    RV_EDITOR_CHECK(ImGui::CalcTextSize("EO").x == 12.0f * scale);
}

} // namespace

RV_EDITOR_TEST(ui_font_uses_pixel_glyphs_and_matching_metrics_at_each_scale)
{
    rv_editor_test_imgui gui(true);
    ImFontAtlas &atlas = *ImGui::GetIO().Fonts;
    for (const float scale : { 1.0f, 1.5f, 2.0f }) {
        const int added = rv_editor_fonts_add(atlas, scale);
        RV_EDITOR_CHECK(added == RV_OK);
        if (added != RV_OK) {
            continue;
        }
        ImGui::GetStyle().FontSizeBase = rv_editor_font_ui()->LegacySize;
        // ImGui only updates atlases which the current context owns.
        ImFontAtlasUpdateNewFrame(&atlas, atlas.Builder == nullptr ? 0 : atlas.Builder->FrameCount + 1, true);
        gui.frame([&] {
            RV_EDITOR_CHECK(ImGui::GetFont() == rv_editor_font_ui());
            RV_EDITOR_CHECK(ImGui::GetFontSize() == 8.0f * scale);

            if (scale == 1.5f) {
                check_fractional_font(atlas);
            } else {
                check_whole_scale_font(atlas, static_cast<int>(scale));
            }
        });
    }

    // Other editor tests share this atlas and expect the default scale.
    RV_EDITOR_CHECK(rv_editor_fonts_add(atlas, 1.0f) == RV_OK);
}
