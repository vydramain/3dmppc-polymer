// A TrueType builder for pdklib's 5x7 font, in memory so ImGui loads it with
// AddFontFromMemoryTTF (0001: no imgui_internal.h).
// Every lit pixel becomes a square outline on a whole-unit grid, as in PxPlus:
// edges land on pixel boundaries and nothing is smoothed.

#include "font/rv_editor_font_ttf.hpp"

#include <algorithm>
#include <cmath>
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

// Point is on the curve: OpenType spec, 'glyf' table.
constexpr int glyf_flag_on_curve = 0x01;
// Points per rectangular contour.
constexpr int glyf_contour_points = 4;
// End point index in 4-point contour (0-indexed).
constexpr int glyf_contour_end_point = 3;

// Cell width in pixels per byte: pdklib raster font cell representation.
constexpr int cell_width_bits = 8;
// Leftmost pixel mask in byte (bit 7).
constexpr uint32_t cell_leftmost_bit = 0x80u;
// Full byte value mask.
constexpr uint32_t byte_value_mask = 0xff;

// Big-endian word serialization: bit shift widths.
// Bits from word to halfword
constexpr int shift_word_to_halfword = 16;
// Bits from halfword to byte
constexpr int shift_halfword_to_byte = 8;

// Floating-point rounding: coordinate conversion from double scale.
// Add before floor() for round-half-up
constexpr double rounding_offset = 0.5;

// Memory alignment: binary format word boundaries.
// 32-bit word boundary (4 bytes)
constexpr int word_alignment = 4;
// 16-bit halfword boundary (2 bytes)
constexpr int halfword_alignment = 2;

// Pairs (min, max) for bounds coordinates.
constexpr int bounds_coord_pairs = 2;
// Total bounds fields (xMin, yMin, xMax, yMax).
constexpr int bounds_field_count = 4;

constexpr int rv_editor_ttf_unit = 64;

// Version fixed 1.0: OpenType spec, 'head' table.
constexpr uint32_t head_version = 0x00010000;
// Magic number.
constexpr uint32_t head_magic_number = 0x5F0F3CF5;
// Baseline at y=0, integer ppem, lsb at x=0.
constexpr uint16_t head_flags = 0x000B;
// Directional hint.
constexpr uint16_t head_font_direction_hint = 2;

// Version 0.5: OpenType spec, 'maxp' table.
constexpr uint32_t maxp_version = 0x00005000;

// Windows platform: OpenType spec, 'cmap' table.
constexpr uint16_t cmap_platform_id = 3;
// Unicode BMP.
constexpr uint16_t cmap_encoding_id = 1;
// Format 4 (segment to delta).
constexpr uint16_t cmap_format = 4;
// Subtable header size.
constexpr uint16_t cmap_header_length = 12;
// End of cmap segments.
constexpr uint16_t cmap_segment_end_marker = 0xFFFF;
// Multiplier for segCountX2.
constexpr uint16_t cmap_segment_count_multiplier = 2;
// Length field offset in format 4 subtable (bytes 2..3 after format).
constexpr int cmap_length_field_offset = 2;

// SFnt version fixed 1.0: OpenType spec, 'Offset Table'.
constexpr uint32_t ttf_sfnt_version = 0x00010000;
// searchRange for table count.
constexpr uint16_t ttf_search_range = 64;
// entrySelector for table count.
constexpr uint16_t ttf_entry_selector = 2;
// Offset table header size.
constexpr uint16_t ttf_header_size = 12;
// Bytes per table directory entry.
constexpr uint16_t ttf_catalog_entry_size = 16;
// Tag length in table directory (OpenType spec 'Table Directory').
constexpr int table_tag_bytes = 4;

// Bytes per glyph in rv_pdklib.
constexpr size_t glyph_data_stride = 8;

// First code point: Unicode standard, Cyrillic block.
constexpr uint32_t cyrillic_range_start = 0x0400;
// Last code point.
constexpr uint32_t cyrillic_range_end = 0x045f;

// Source pixel edge i lands on a whole target pixel by a fixed nearest rule (halves round up), so each
// source pixel covers a whole number of target pixels and nothing is smoothed.
int rv_editor_ttf_edge(int i, double scale)
{
    return static_cast<int>(std::floor(i * scale + rounding_offset)) * rv_editor_ttf_unit;
}

struct rv_editor_ttf_glyph {
    const uint8_t *rows = nullptr; // cell height bytes, top row first, high bit leftmost; nullptr for an empty glyph
};

// Big-endian writer.
struct rv_editor_ttf_out {
    std::string b;
    void u8(uint32_t v)
    {
        b.push_back(static_cast<char>(v & byte_value_mask));
    }
    void u16(uint32_t v)
    {
        u8(v >> shift_halfword_to_byte);
        u8(v);
    }
    void u32(uint32_t v)
    {
        u16(v >> shift_word_to_halfword);
        u16(v);
    }
    void pad4()
    {
        while (b.size() % word_alignment != 0) {
            u8(0);
        }
    }
};

// One glyph's glyf record: a rectangle contour per horizontal run of lit pixels.
std::string rv_editor_ttf_glyf(const rv_editor_ttf_glyph &g, int cell_h, double scale, int16_t bounds[4])
{
    struct run {
        int x0, x1, y0, y1;
    };
    std::vector<run> runs;
    for (int row = 0; g.rows != nullptr && row < cell_h; ++row) {
        const uint8_t bits = g.rows[row];
        for (int x = 0; x < cell_width_bits;) {
            if ((bits & (cell_leftmost_bit >> x)) == 0) {
                ++x;
                continue;
            }
            const int start = x;
            while (x < cell_width_bits && (bits & (cell_leftmost_bit >> x)) != 0) {
                ++x;
            }
            // Row 0 is the top of the cell; font y grows upward from the cell's bottom.
            runs.push_back({ rv_editor_ttf_edge(start, scale),
                rv_editor_ttf_edge(x, scale),
                rv_editor_ttf_edge(cell_h - row - 1, scale),
                rv_editor_ttf_edge(cell_h - row, scale) });
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
        o.u16(static_cast<uint32_t>(k * glyf_contour_points + glyf_contour_end_point)); // end point of each 4-point contour
    }
    o.u16(0); // no instructions
    for (size_t k = 0; k < runs.size() * glyf_contour_points; ++k) {
        o.u8(glyf_flag_on_curve); // on curve, 16-bit x and y deltas
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
    while (o.b.size() % halfword_alignment != 0) {
        o.u8(0);
    }
    return o.b;
}

// A TrueType file of `glyphs` in a source cell_w x cell_h cell drawn at `scale` target pixels per source
// pixel; glyph 0 is notdef. The em is the scaled cell height, so size = em pixels draws 1 unit grid : 1 px.
std::string rv_editor_ttf_build(const std::vector<rv_editor_ttf_glyph> &glyphs,
    const std::vector<std::pair<uint32_t, uint16_t>> &map,
    int src_cell_w,
    int src_cell_h,
    double scale)
{
    const int cell_h = static_cast<int>(std::floor(src_cell_h * scale + rounding_offset));
    const int cell_w = static_cast<int>(std::floor(src_cell_w * scale + rounding_offset));
    // `map` is in increasing code point order, as cmap format 4 needs.
    std::string glyf;
    // glyf and loca.
    std::vector<uint32_t> loca;
    std::vector<int16_t> lsb;
    int16_t font_bounds[4] = { 0, 0, 0, 0 };
    for (const rv_editor_ttf_glyph &g : glyphs) {
        loca.push_back(static_cast<uint32_t>(glyf.size()));
        int16_t b[4];
        glyf += rv_editor_ttf_glyf(g, src_cell_h, scale, b);
        lsb.push_back(b[0]);
        for (int k = 0; k < bounds_coord_pairs; ++k) {
            font_bounds[k] = std::min(font_bounds[k], b[k]);
            font_bounds[k + bounds_coord_pairs] = std::max(font_bounds[k + bounds_coord_pairs], b[k + bounds_coord_pairs]);
        }
    }
    loca.push_back(static_cast<uint32_t>(glyf.size()));
    const uint16_t count = static_cast<uint16_t>(glyphs.size());
    const int cell_w_units = cell_w * rv_editor_ttf_unit;
    const int cell_h_units = cell_h * rv_editor_ttf_unit;

    rv_editor_ttf_out head;
    head.u32(head_version);
    head.u32(head_version);
    head.u32(0);
    head.u32(head_magic_number);
    head.u16(head_flags);
    head.u16(static_cast<uint32_t>(cell_h_units));
    for (int k = 0; k < bounds_field_count; ++k) {
        head.u32(0);
    }
    for (int16_t v : font_bounds) {
        head.u16(static_cast<uint16_t>(v));
    }
    head.u16(0);
    head.u16(static_cast<uint32_t>(cell_h));
    head.u16(head_font_direction_hint);
    head.u16(1);
    head.u16(0);

    // The cell sits on the baseline: ascent is the whole cell, descent none.
    rv_editor_ttf_out hhea;
    hhea.u32(head_version);
    hhea.u16(static_cast<uint32_t>(cell_h_units));
    hhea.u16(0);
    hhea.u16(0);
    hhea.u16(static_cast<uint32_t>(cell_w_units));
    hhea.u16(0);
    hhea.u16(0);
    hhea.u16(static_cast<uint16_t>(font_bounds[2]));
    hhea.u16(1);
    hhea.u16(0);
    hhea.u16(0);
    for (int k = 0; k < bounds_field_count; ++k) {
        hhea.u16(0);
    }
    hhea.u16(0);
    hhea.u16(count);

    rv_editor_ttf_out hmtx;
    for (uint16_t k = 0; k < count; ++k) {
        hmtx.u16(static_cast<uint32_t>(cell_w_units));
        hmtx.u16(static_cast<uint16_t>(lsb[k]));
    }

    rv_editor_ttf_out maxp;
    maxp.u32(maxp_version);
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
    cmap.u16(cmap_platform_id);
    cmap.u16(cmap_encoding_id);
    cmap.u32(cmap_header_length);
    const size_t sub = cmap.b.size();
    cmap.u16(cmap_format);
    cmap.u16(0);
    cmap.u16(0);
    uint16_t search = 1;
    uint16_t selector = 0;
    while (search * cmap_segment_count_multiplier <= segs) {
        search = static_cast<uint16_t>(search * cmap_segment_count_multiplier);
        ++selector;
    }
    cmap.u16(static_cast<uint16_t>(segs * cmap_segment_count_multiplier));
    cmap.u16(static_cast<uint16_t>(search * cmap_segment_count_multiplier));
    cmap.u16(selector);
    cmap.u16(static_cast<uint16_t>((segs - search) * cmap_segment_count_multiplier));
    for (const auto &[code, glyph] : map) {
        cmap.u16(code);
    }
    cmap.u16(cmap_segment_end_marker);
    cmap.u16(0);
    for (const auto &[code, glyph] : map) {
        cmap.u16(code);
    }
    cmap.u16(cmap_segment_end_marker);
    for (const auto &[code, glyph] : map) {
        cmap.u16(static_cast<uint16_t>(glyph - code)); // idDelta, modulo 65536
    }
    cmap.u16(1);
    for (uint16_t k = 0; k < segs; ++k) {
        cmap.u16(0); // idRangeOffset: none, idDelta says it all
    }
    const size_t len = cmap.b.size() - sub;
    cmap.b[sub + cmap_length_field_offset] = static_cast<char>((len >> shift_halfword_to_byte) & byte_value_mask);
    cmap.b[sub + cmap_length_field_offset + 1] = static_cast<char>(len & byte_value_mask);

    // The file: offset table, directory sorted by tag, tables 4-byte aligned.
    const std::pair<const char *, const std::string *> tables[] = { { "cmap", &cmap.b },
        { "glyf", &glyf },
        { "head", &head.b },
        { "hhea", &hhea.b },
        { "hmtx", &hmtx.b },
        { "loca", &locat.b },
        { "maxp", &maxp.b } };
    const uint16_t n = static_cast<uint16_t>(std::size(tables));
    rv_editor_ttf_out file;
    file.u32(ttf_sfnt_version);
    file.u16(n);
    file.u16(ttf_search_range);
    file.u16(ttf_entry_selector);
    file.u16(static_cast<uint32_t>(n * ttf_catalog_entry_size - ttf_search_range));
    uint32_t offset = ttf_header_size + static_cast<uint32_t>(ttf_catalog_entry_size) * n;
    std::string body;
    for (const auto &[tag, data] : tables) {
        file.b.append(tag, table_tag_bytes);
        file.u32(0);
        file.u32(offset);
        file.u32(static_cast<uint32_t>(data->size()));
        body += *data;
        while (body.size() % word_alignment != 0) {
            body.push_back('\0');
        }
        offset = ttf_header_size + static_cast<uint32_t>(ttf_catalog_entry_size) * n + static_cast<uint32_t>(body.size());
    }
    return file.b + body;
}

} // namespace

int rv_editor_font_ttf_em(double scale)
{
    return static_cast<int>(std::floor(rv_pdklib::rv_font_cell_height * scale + rounding_offset));
}

std::string rv_editor_font_ttf(double scale)
{
    // Glyph 0 is notdef; then ASCII 32..126, then the Cyrillic block by slot.
    std::vector<rv_editor_ttf_glyph> glyphs;
    glyphs.push_back({ &rv_pdklib::rv_font_bits[rv_pdklib::rv_font_notdef_index * glyph_data_stride] });
    for (int code = rv_pdklib::rv_font_first_code; code <= rv_pdklib::rv_font_last_code; ++code) {
        glyphs.push_back({ &rv_pdklib::rv_font_bits[(code - rv_pdklib::rv_font_first_code) * glyph_data_stride] });
    }
    const size_t cyrillic_first = glyphs.size();
    for (int slot = 0; slot < rv_pdklib::rv_font_cyrillic_glyph_count; ++slot) {
        glyphs.push_back({ &rv_pdklib::rv_font_cyrillic_bits[slot * glyph_data_stride] });
    }

    // Code point -> glyph, in code point order, for cmap format 4.
    std::vector<std::pair<uint32_t, uint16_t>> map;
    for (int code = rv_pdklib::rv_font_first_code; code <= rv_pdklib::rv_font_last_code; ++code) {
        map.push_back({ static_cast<uint32_t>(code), static_cast<uint16_t>(code - rv_pdklib::rv_font_first_code + 1) });
    }
    for (uint32_t code = cyrillic_range_start; code <= cyrillic_range_end; ++code) {
        const int slot = rv_pdklib::rv_font_cyrillic_slot(code);
        if (slot >= 0) {
            map.push_back({ code, static_cast<uint16_t>(cyrillic_first + static_cast<size_t>(slot)) });
        }
    }

    // Every glyph but notdef keeps its ink in the cell's left five columns: a
    // 6 px advance leaves one column between letters instead of three.
    return rv_editor_ttf_build(glyphs, map, rv_editor_font_ui_advance, rv_pdklib::rv_font_cell_height, scale);
}

} // namespace rv_editor
