// pdklib's 5x7 bitmap font as a TrueType file in memory, so ImGui loads it with
// its public AddFontFromMemoryTTF (0001: no imgui_internal.h). Every lit pixel
// becomes a square outline on a whole-unit grid, as in PxPlus: at 8 px per cell
// the edges land on pixel boundaries and nothing is smoothed.

#include "font/rv_editor_font_ttf.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pdklib/rv_font/rv_font_cyrillic.hpp"
#include "pdklib/rv_font/rv_font_data.hpp"

namespace rv_editor
{

namespace
{

constexpr int rv_editor_ttf_unit = 64; // font units per pixel
constexpr int rv_editor_ttf_cell_w = rv_pdklib::rv_font_cell_width * rv_editor_ttf_unit;
constexpr int rv_editor_ttf_cell_h = rv_pdklib::rv_font_cell_height * rv_editor_ttf_unit;

struct rv_editor_ttf_glyph
{
    const uint8_t *rows = nullptr; // 8 bytes, top row first, high bit leftmost; nullptr for an empty glyph
};

// Big-endian writer.
struct rv_editor_ttf_out
{
    std::string b;
    void u8(uint32_t v) { b.push_back(static_cast<char>(v & 0xff)); }
    void u16(uint32_t v)
    {
        u8(v >> 8);
        u8(v);
    }
    void u32(uint32_t v)
    {
        u16(v >> 16);
        u16(v);
    }
    void pad4()
    {
        while (b.size() % 4 != 0) {
            u8(0);
        }
    }
};

// One glyph's glyf record: a rectangle contour per horizontal run of lit pixels.
std::string rv_editor_ttf_glyf(const rv_editor_ttf_glyph &g, int16_t bounds[4])
{
    struct run
    {
        int x0, x1, y0, y1;
    };
    std::vector<run> runs;
    for (int row = 0; g.rows != nullptr && row < rv_pdklib::rv_font_cell_height; ++row) {
        const uint8_t bits = g.rows[row];
        for (int x = 0; x < 8;) {
            if ((bits & (0x80u >> x)) == 0) {
                ++x;
                continue;
            }
            const int start = x;
            while (x < 8 && (bits & (0x80u >> x)) != 0) {
                ++x;
            }
            // Row 0 is the top of the cell; font y grows upward from the cell's bottom.
            const int top = (rv_pdklib::rv_font_cell_height - row) * rv_editor_ttf_unit;
            runs.push_back({ start * rv_editor_ttf_unit, x * rv_editor_ttf_unit, top - rv_editor_ttf_unit, top });
        }
    }
    bounds[0] = bounds[1] = bounds[2] = bounds[3] = 0;
    if (runs.empty()) {
        return {};
    }
    int x_min = runs[0].x0;
    int y_min = runs[0].y0;
    int x_max = runs[0].x1;
    int y_max = runs[0].y1;
    for (const run &r : runs) {
        x_min = std::min(x_min, r.x0);
        y_min = std::min(y_min, r.y0);
        x_max = std::max(x_max, r.x1);
        y_max = std::max(y_max, r.y1);
    }
    bounds[0] = static_cast<int16_t>(x_min);
    bounds[1] = static_cast<int16_t>(y_min);
    bounds[2] = static_cast<int16_t>(x_max);
    bounds[3] = static_cast<int16_t>(y_max);

    rv_editor_ttf_out o;
    o.u16(static_cast<uint32_t>(runs.size()));
    for (int v : { x_min, y_min, x_max, y_max }) {
        o.u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
    for (size_t k = 0; k < runs.size(); ++k) {
        o.u16(static_cast<uint32_t>(k * 4 + 3)); // end point of each 4-point contour
    }
    o.u16(0); // no instructions
    for (size_t k = 0; k < runs.size() * 4; ++k) {
        o.u8(0x01); // on curve, 16-bit x and y deltas
    }
    // Clockwise: bottom-left, top-left, top-right, bottom-right.
    int px = 0;
    for (const run &r : runs) {
        for (int x : { r.x0, r.x0, r.x1, r.x1 }) {
            o.u16(static_cast<uint16_t>(static_cast<int16_t>(x - px)));
            px = x;
        }
    }
    int py = 0;
    for (const run &r : runs) {
        for (int y : { r.y0, r.y1, r.y1, r.y0 }) {
            o.u16(static_cast<uint16_t>(static_cast<int16_t>(y - py)));
            py = y;
        }
    }
    while (o.b.size() % 2 != 0) {
        o.u8(0);
    }
    return o.b;
}

} // namespace

std::string rv_editor_font_ttf()
{
    // Glyph 0 is notdef; then ASCII 32..126, then the Cyrillic block by slot.
    std::vector<rv_editor_ttf_glyph> glyphs;
    glyphs.push_back({ &rv_pdklib::rv_font_bits[rv_pdklib::rv_font_notdef_index * 8] });
    for (int code = rv_pdklib::rv_font_first_code; code <= rv_pdklib::rv_font_last_code; ++code) {
        glyphs.push_back({ &rv_pdklib::rv_font_bits[(code - rv_pdklib::rv_font_first_code) * 8] });
    }
    const size_t cyrillic_first = glyphs.size();
    for (int slot = 0; slot < rv_pdklib::rv_font_cyrillic_glyph_count; ++slot) {
        glyphs.push_back({ &rv_pdklib::rv_font_cyrillic_bits[slot * 8] });
    }

    // Code point -> glyph, in code point order, for cmap format 4.
    std::vector<std::pair<uint32_t, uint16_t>> map;
    for (int code = rv_pdklib::rv_font_first_code; code <= rv_pdklib::rv_font_last_code; ++code) {
        map.push_back({ static_cast<uint32_t>(code), static_cast<uint16_t>(code - rv_pdklib::rv_font_first_code + 1) });
    }
    for (uint32_t code = 0x0400; code <= 0x045f; ++code) {
        const int slot = rv_pdklib::rv_font_cyrillic_slot(code);
        if (slot >= 0) {
            map.push_back({ code, static_cast<uint16_t>(cyrillic_first + static_cast<size_t>(slot)) });
        }
    }

    // glyf and loca.
    std::string glyf;
    std::vector<uint32_t> loca;
    std::vector<int16_t> lsb;
    int16_t font_bounds[4] = { 0, 0, 0, 0 };
    for (const rv_editor_ttf_glyph &g : glyphs) {
        loca.push_back(static_cast<uint32_t>(glyf.size()));
        int16_t b[4];
        glyf += rv_editor_ttf_glyf(g, b);
        lsb.push_back(b[0]);
        for (int k = 0; k < 2; ++k) {
            font_bounds[k] = std::min(font_bounds[k], b[k]);
            font_bounds[k + 2] = std::max(font_bounds[k + 2], b[k + 2]);
        }
    }
    loca.push_back(static_cast<uint32_t>(glyf.size()));
    const uint16_t count = static_cast<uint16_t>(glyphs.size());

    rv_editor_ttf_out head;
    head.u32(0x00010000);
    head.u32(0x00010000);
    head.u32(0);          // checkSumAdjustment: nobody here checks it
    head.u32(0x5F0F3CF5); // magic
    head.u16(0x000B);     // baseline at y=0, integer ppem, lsb at x=0
    head.u16(rv_editor_ttf_cell_h); // unitsPerEm: one em is one cell high
    for (int k = 0; k < 4; ++k) {
        head.u32(0); // created, modified
    }
    for (int16_t v : font_bounds) {
        head.u16(static_cast<uint16_t>(v));
    }
    head.u16(0);  // macStyle
    head.u16(8);  // lowestRecPPEM
    head.u16(2);  // fontDirectionHint
    head.u16(1);  // indexToLocFormat: long
    head.u16(0);  // glyphDataFormat

    // The cell sits on the baseline: ascent is the whole cell, descent none.
    rv_editor_ttf_out hhea;
    hhea.u32(0x00010000);
    hhea.u16(rv_editor_ttf_cell_h);
    hhea.u16(0);
    hhea.u16(0);
    hhea.u16(rv_editor_ttf_cell_w);
    hhea.u16(0);
    hhea.u16(0);
    hhea.u16(static_cast<uint16_t>(font_bounds[2]));
    hhea.u16(1);
    hhea.u16(0);
    hhea.u16(0);
    for (int k = 0; k < 4; ++k) {
        hhea.u16(0);
    }
    hhea.u16(0);
    hhea.u16(count);

    rv_editor_ttf_out hmtx;
    for (uint16_t k = 0; k < count; ++k) {
        hmtx.u16(rv_editor_ttf_cell_w);
        hmtx.u16(static_cast<uint16_t>(lsb[k]));
    }

    rv_editor_ttf_out maxp;
    maxp.u32(0x00005000);
    maxp.u16(count);

    rv_editor_ttf_out locat;
    for (uint32_t v : loca) {
        locat.u32(v);
    }

    // cmap: Windows Unicode BMP, format 4, one segment per code point plus the end marker.
    const uint16_t segs = static_cast<uint16_t>(map.size() + 1);
    rv_editor_ttf_out cmap;
    cmap.u16(0);
    cmap.u16(1);
    cmap.u16(3);
    cmap.u16(1);
    cmap.u32(12);
    const size_t sub = cmap.b.size();
    cmap.u16(4);
    cmap.u16(0); // length, patched below
    cmap.u16(0);
    uint16_t search = 1;
    uint16_t selector = 0;
    while (search * 2 <= segs) {
        search = static_cast<uint16_t>(search * 2);
        ++selector;
    }
    cmap.u16(static_cast<uint16_t>(segs * 2));
    cmap.u16(static_cast<uint16_t>(search * 2));
    cmap.u16(selector);
    cmap.u16(static_cast<uint16_t>((segs - search) * 2));
    for (const auto &[code, glyph] : map) {
        cmap.u16(code); // endCode
    }
    cmap.u16(0xFFFF);
    cmap.u16(0); // reservedPad
    for (const auto &[code, glyph] : map) {
        cmap.u16(code); // startCode
    }
    cmap.u16(0xFFFF);
    for (const auto &[code, glyph] : map) {
        cmap.u16(static_cast<uint16_t>(glyph - code)); // idDelta, modulo 65536
    }
    cmap.u16(1);
    for (uint16_t k = 0; k < segs; ++k) {
        cmap.u16(0); // idRangeOffset: none, idDelta says it all
    }
    const size_t len = cmap.b.size() - sub;
    cmap.b[sub + 2] = static_cast<char>((len >> 8) & 0xff);
    cmap.b[sub + 3] = static_cast<char>(len & 0xff);

    // The file: offset table, directory sorted by tag, tables 4-byte aligned.
    const std::pair<const char *, const std::string *> tables[] = { { "cmap", &cmap.b }, { "glyf", &glyf },
        { "head", &head.b }, { "hhea", &hhea.b }, { "hmtx", &hmtx.b }, { "loca", &locat.b }, { "maxp", &maxp.b } };
    const uint16_t n = static_cast<uint16_t>(std::size(tables));
    rv_editor_ttf_out file;
    file.u32(0x00010000);
    file.u16(n);
    file.u16(64);
    file.u16(2);
    file.u16(static_cast<uint32_t>(n * 16 - 64));
    uint32_t offset = 12 + 16u * n;
    std::string body;
    for (const auto &[tag, data] : tables) {
        file.b.append(tag, 4);
        file.u32(0); // checksum: nobody here checks it
        file.u32(offset);
        file.u32(static_cast<uint32_t>(data->size()));
        body += *data;
        while (body.size() % 4 != 0) {
            body.push_back('\0');
        }
        offset = 12 + 16u * n + static_cast<uint32_t>(body.size());
    }
    return file.b + body;
}

} // namespace rv_editor
