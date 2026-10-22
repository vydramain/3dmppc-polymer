#pragma once

#include <cstdint>

#include "pdk/cd/rv_cd.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"

namespace example_cpp_pdkkit
{

// Cells of the board's text ("ОСТАЛОСЬ: 5"), counted from 0; a cell may be the space between colon and digit.
constexpr int RV_BOARD_CELL_COUNT = 11;

// True when cell i (0 <= i < RV_BOARD_CELL_COUNT) is lit in frame `frame`: the flicker table decides it,
// the space cell never lights. The sound takes the lit set from here too, so both agree.
bool rv_board_cell_lit(int i, int64_t frame);

// A halo or a letter of the board: shown while its cell is lit.
struct ecs_component_flicker {
    int cell = 0;
};

// Factory of flicker: field cell, an integer in 0..RV_BOARD_CELL_COUNT-1, 0 when absent. Other values are RV_ERR_INVAL.
int ecs_flicker_factory(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, const rv_cd_scene_component &component);

// Frame system: shows each flicker entity whose cell is lit at the frame's place in the route period, hides the others.
// Error: RV_ERR_NOENT when a dark entity cannot take its hidden mark (a stale id); logged with its cell.
int ecs_system_flicker(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit
