#pragma once

#include <cstdint>
#include <string>

#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"

namespace example_cpp_pdkkit
{

// Cells of the board's text ("ОСТАЛОСЬ: 5"), counted from 0; a cell may be the space between colon and digit.
constexpr int RV_BOARD_CELL_COUNT = 11;

// True when cell i (0 <= i < RV_BOARD_CELL_COUNT) is lit in frame `frame`: the flicker table decides it,
// the space cell never lights. The sound takes the lit set from here too, so both agree.
bool rv_board_cell_lit(int i, int64_t frame);

// A halo or a letter of the board: shown while its cell is lit. The generic factory fills cell from the scene;
// cell is 0 when the scene omits it.
struct ecs_component_flicker {
    int32_t cell = 0;

    // Refuses a cell outside 0..RV_BOARD_CELL_COUNT-1; the reason names the cell and the range.
    static int ecs_check(const ecs_component_flicker &flicker, std::string &reason)
    {
        const bool in_range = flicker.cell >= 0 && flicker.cell < RV_BOARD_CELL_COUNT;
        if (!in_range) {
            reason = "cell " + std::to_string(flicker.cell) + " is outside 0.." + std::to_string(RV_BOARD_CELL_COUNT - 1);
            return RV_ERR_INVAL;
        }
        return RV_OK;
    }
};

// Marks the board entity: the scene's board object has it; the disc finds the board by it and passes the result to the sound.
struct ecs_component_board {};

// Scene entity type "board": a group that gets the board marker when it is created.
class ecs_entity_board : public rv_pdkkit::ecs_entity
{
public:
    ecs_entity_board()
        : rv_pdkkit::ecs_entity("board")
    {
    }

    int add_defaults(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id) const override;
};

// Frame system: shows each flicker entity whose cell is lit at the frame's place in the route period, hides the others.
// Error: RV_ERR_NOENT when a dark entity cannot take its hidden mark (a stale id); logged with its cell.
int ecs_system_flicker(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_flicker, "flicker", PDKKIT_FIELD(cell, int32, 0));
