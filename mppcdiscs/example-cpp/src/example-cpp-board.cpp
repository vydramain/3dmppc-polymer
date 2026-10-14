#include "example-cpp-board.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pdk/rv_err.h"
#include "pdklib/rv_math/rv_fx.hpp"
#include "example-cpp-draw.hpp"

namespace example_cpp
{

namespace
{

// The models of the disc (assets/models/*.gltf): one quad per glyph, and the halo quad. Each faces +X,
// so the panel's front (+X) shows them; their pictures are the PNGs in assets/tex.
constexpr const char *RV_BOARD_HALO_MODEL = "halo.mppcmod";

// "ОСТАЛОСЬ: 5", one model per cell; nullptr is the space between the colon and the digit.
constexpr const char *RV_BOARD_TEXT[] = {
    "glyph_o.mppcmod",
    "glyph_s.mppcmod",
    "glyph_t.mppcmod",
    "glyph_a.mppcmod",
    "glyph_l.mppcmod",
    "glyph_o.mppcmod",
    "glyph_s.mppcmod",
    "glyph_soft.mppcmod",
    "glyph_colon.mppcmod",
    nullptr,
    "glyph_5.mppcmod",
};
constexpr int RV_BOARD_TEXT_LEN = static_cast<int>(std::size(RV_BOARD_TEXT));
static_assert(RV_BOARD_TEXT_LEN == RV_BOARD_CELL_COUNT, "the header's cell count is the text's length");
static_assert(RV_BOARD_TEXT_LEN % 2 == 1, "the text has a centre cell");

// Q20.12 raw values (q; 1 m = 4096 q). Cell pitch along the panel: 983 q (~0.24 m; 0.24 * 4096 = 983.04).
// A glyph quad is 0.24 x 0.30.
constexpr int64_t RV_BOARD_CELL_WIDTH = 983;
// Cell i sits at (i - centre) * pitch, so the text is centred on the panel; centre is the middle cell.
constexpr int64_t RV_BOARD_CENTRE_CELL = (RV_BOARD_TEXT_LEN - 1) / 2;

// Forward offsets from the panel along its normal: 20 q (~0.005 m; 20.48 rounded) and 41 q (~0.010 m;
// 40.96 rounded). They set the geometry (the halo and the letter stand off the panel). The sort inside
// one depth key is the submission order, so all halos are put before all letters (rv_board_draw).
constexpr rv_pdklib::fx RV_BOARD_HALO_FORWARD = 20;
constexpr rv_pdklib::fx RV_BOARD_LETTER_FORWARD = 41;

// Flicker: letter i is lit when table[(frame / STEP + i * SHIFT) % size] > LIT_ABOVE.
// The shift spreads the letters over the table so they do not blink in unison.
constexpr int64_t RV_BOARD_FLICKER_STEP = 6; // frames per table step
constexpr int64_t RV_BOARD_FLICKER_SHIFT = 37;
constexpr uint8_t RV_BOARD_LIT_ABOVE = 128;

// Own values, of the same kind as Doom's rndtable (256 bytes of pseudo-random data read by
// frame index), not its numbers. A letter that is out is not drawn: the models have no tint.
// clang-format off: one table row per line, for reading. to_array takes the size from the list.
constexpr auto RV_BOARD_FLICKER_TABLE = std::to_array<uint8_t>({
    37,  201, 88,  14,  166, 243, 59,  117, 9,   180, 128, 72,  223, 45,  154, 96,  231, 6,   112, 190, 53,  147, 25,  210, 84,  169, 13,  238, 101, 66,  197, 31,
    142, 77,  255, 20,  178, 98,  230, 4,   133, 61,  215, 110, 39,  186, 92,  249, 17,  164, 121, 57,  208, 83,  2,   141, 226, 70,  105, 189, 44,  153, 27,  219,
    91,  198, 36,  170, 129, 252, 65,  12,  183, 97,  240, 58,  146, 107, 200, 8,   174, 119, 48,  233, 80,  161, 22,  205, 137, 74,  254, 33,  118, 194, 63,  211,
    3,   149, 90,  181, 226, 116, 43,  167, 102, 239, 21,  156, 85,  209, 130, 49,  192, 28,  224, 75,  158, 127, 62,  245, 16,  187, 99,  136, 233, 71,  172, 38,
    109, 207, 52,  143, 3,   218, 124, 89,  177, 40,  196, 103, 60,  230, 150, 15,  221, 86,  163, 29,  115, 201, 47,  185, 138, 253, 94,  176, 26,  123, 214, 68,
    134, 1,   199, 106, 244, 56,  160, 19,  228, 81,  145, 212, 35,  191, 113, 250, 54,  171, 79,  236, 148, 11,  195, 126, 100, 168, 30,  223, 73,  139, 5,   182,
    232, 111, 42,  157, 206, 87,  246, 64,  179, 23,  132, 108, 216, 50,  202, 93,  155, 24,  241, 67,  188, 135, 10,  225, 76,  162, 46,  247, 120, 193, 78,  144,
    41,  220, 104, 175, 32,  237, 122, 88,  213, 151, 59,  18,  203, 140, 69,  234, 165, 95,  248, 57,  198, 4,   159, 210, 131, 83,  242, 34,  184, 101, 229, 114,
});
// clang-format on
static_assert(RV_BOARD_FLICKER_TABLE.size() == 256, "the flicker table is 256 bytes");
constexpr int64_t RV_BOARD_FLICKER_SIZE = static_cast<int64_t>(RV_BOARD_FLICKER_TABLE.size());

// Row-major world matrix of cell i's model on the panel: board * translate(place), where place is
// (forward along the board's +x, the cell's place along +z). For a board that is a scene root this is
// rv_scene_world of a child with no rotation and no scale; a nested board multiplies its chain in
// another order, so the equality holds only for a root.
int64_t panel_world(const float *board, rv_pdklib::fx forward, int i, float out[16])
{
    const int64_t along_raw = (static_cast<int64_t>(i) - RV_BOARD_CENTRE_CELL) * RV_BOARD_CELL_WIDTH;
    rv_pdklib::fx along = 0;
    if (rv_pdklib::fx_from_raw(along_raw, along) != RV_OK) {
        return RV_ERR_INVAL;
    }
    const rv_pdklib::rv_mat4 place =
        rv_pdklib::rv_mat4_translate(rv_pdklib::fx_vec3_to_float(rv_pdklib::fx_vec3{ forward, 0, along }));
    rv_pdklib::rv_mat4 board_world{};
    std::memcpy(board_world.m, board, sizeof(board_world.m));
    const rv_pdklib::rv_mat4 product = rv_pdklib::rv_mat4_mul(board_world, place);
    std::memcpy(out, product.m, sizeof(product.m));
    return RV_OK;
}

} // namespace

bool rv_board_cell_lit(int i, int64_t frame)
{
    const int64_t index = (frame / RV_BOARD_FLICKER_STEP + i * RV_BOARD_FLICKER_SHIFT) % RV_BOARD_FLICKER_SIZE;
    const uint8_t level = RV_BOARD_FLICKER_TABLE[static_cast<size_t>(index)];
    return RV_BOARD_TEXT[i] != nullptr && level > RV_BOARD_LIT_ABOVE;
}

int64_t rv_board_draw(rv_pdko *pdk, const float *world, int64_t frame)
{
    std::array<bool, RV_BOARD_TEXT_LEN> lit = {};
    for (int i = 0; i < RV_BOARD_TEXT_LEN; ++i) {
        lit[static_cast<size_t>(i)] = rv_board_cell_lit(i, frame);
    }

    // Halos first, then letters: a halo must not land over its neighbour's letter.
    for (int i = 0; i < RV_BOARD_TEXT_LEN; ++i) {
        if (!lit[static_cast<size_t>(i)]) {
            continue;
        }
        float halo_world[16] = {};
        const int64_t halo_place = panel_world(world, RV_BOARD_HALO_FORWARD, i, halo_world);
        if (halo_place != RV_OK) {
            return halo_place;
        }
        const int64_t halo_code = rv_draw_decal(pdk, RV_BOARD_HALO_MODEL, halo_world);
        if (halo_code < 0) {
            return halo_code;
        }
    }

    for (int i = 0; i < RV_BOARD_TEXT_LEN; ++i) {
        if (!lit[static_cast<size_t>(i)]) {
            continue;
        }
        float letter_world[16] = {};
        const int64_t letter_place = panel_world(world, RV_BOARD_LETTER_FORWARD, i, letter_world);
        if (letter_place != RV_OK) {
            return letter_place;
        }
        const int64_t letter_code = rv_draw_decal(pdk, RV_BOARD_TEXT[i], letter_world);
        if (letter_code < 0) {
            return letter_code;
        }
    }
    return RV_OK;
}

} // namespace example_cpp
