#include "example-cpp-pdkkit-board.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "example-cpp-pdkkit-route.hpp"

namespace example_cpp_pdkkit
{

namespace
{

// The scene field that names the cell of a flicker entity, and the cell it takes when the field is absent.
constexpr const char *RV_FLICKER_CELL_KEY = "cell";
constexpr int64_t RV_FLICKER_CELL_DEFAULT = 0;

// Cell 9 of "ОСТАЛОСЬ: 5" is the space between the colon and the digit: the scene has no entity for it.
constexpr int RV_BOARD_SPACE_CELL = 9;
static_assert(RV_BOARD_SPACE_CELL < RV_BOARD_CELL_COUNT, "the space cell is one of the board's cells");

// Flicker: cell i is lit when table[(frame / STEP + i * SHIFT) % size] > LIT_ABOVE.
// The shift spreads the cells over the table so they do not blink in unison.
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

// A flicker entity and whether its cell is lit at the frame.
struct flicker_state {
    rv_pdkkit::ecs_id id{};
    int cell = 0;
    bool lit = false;
};

} // namespace

bool rv_board_cell_lit(int i, int64_t frame)
{
    const int64_t index = (frame / RV_BOARD_FLICKER_STEP + i * RV_BOARD_FLICKER_SHIFT) % RV_BOARD_FLICKER_SIZE;
    const uint8_t level = RV_BOARD_FLICKER_TABLE[static_cast<size_t>(index)];
    return i != RV_BOARD_SPACE_CELL && level > RV_BOARD_LIT_ABOVE;
}

int ecs_flicker_factory(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, const rv_pdkkit::ecs_scene_component &component)
{
    int64_t cell = RV_FLICKER_CELL_DEFAULT;
    const int read_code = rv_pdkkit::ecs_field_int(component, RV_FLICKER_CELL_KEY, cell);
    const bool absent = read_code == RV_ERR_NOENT;
    if (read_code != RV_OK && !absent) {
        RV_LOG_ERR("board", "flicker: field {} is not an integer: rv_err {}", RV_FLICKER_CELL_KEY, read_code);
        return read_code;
    }
    const bool in_range = cell >= 0 && cell < RV_BOARD_CELL_COUNT;
    if (!in_range) {
        RV_LOG_ERR("board", "flicker: cell {} is outside 0..{}", cell, RV_BOARD_CELL_COUNT - 1);
        return RV_ERR_INVAL;
    }
    const int add_code =
        rv_pdkkit::ecs_component_add<ecs_component_flicker>(world, id, ecs_component_flicker{ static_cast<int>(cell) });
    if (add_code != RV_OK) {
        RV_LOG_ERR("board", "flicker: entity cannot take the component: rv_err {}", add_code);
        return add_code;
    }
    return RV_OK;
}

int ecs_system_flicker(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    const int64_t phase = frame.frame % RV_ROUTE_PERIOD_FRAMES;
    // Collected first: the hidden mark is added and removed only after the walk.
    std::vector<flicker_state> states;
    rv_pdkkit::ecs_component_each<ecs_component_flicker>(world,
        [&states, phase](rv_pdkkit::ecs_id id, ecs_component_flicker &flicker) {
            states.push_back(flicker_state{ id, flicker.cell, rv_board_cell_lit(flicker.cell, phase) });
        });
    for (const flicker_state &state : states) {
        if (state.lit) {
            // NOENT: the entity has no hidden mark, so it is shown already.
            rv_pdkkit::ecs_component_remove<rv_pdkkit::ecs_component_hidden>(world, state.id);
            continue;
        }
        const rv_pdkkit::ecs_component_hidden *hidden =
            rv_pdkkit::ecs_component_ensure<rv_pdkkit::ecs_component_hidden>(world, state.id);
        if (hidden == nullptr) {
            // Named: an unscoped enum does not format.
            const int code = RV_ERR_NOENT;
            RV_LOG_ERR("board", "flicker: cell {} cannot be hidden, stale entity: rv_err {}", state.cell, code);
            return code;
        }
    }
    return RV_OK;
}

} // namespace example_cpp_pdkkit
