#pragma once

#include <cstdint>

#include "pdk/rv_pdko.h"

namespace example_cpp
{

// Cells of the board's text ("ОСТАЛОСЬ: 5"), counted from 0; a cell may be the space between colon and digit.
constexpr int RV_BOARD_CELL_COUNT = 11;

// True when cell i (0 <= i < RV_BOARD_CELL_COUNT) is lit in frame `frame`: the flicker table decides it,
// the space cell never lights. The sound takes the lit set from here too, so both agree.
bool rv_board_cell_lit(int i, int64_t frame);

// Draws the lettering of the factory board on the board object's panel: lit letters
// and their halo, frame is the demo clock within its period. world is the object's
// world matrix, row-major as rv_cg_put. Returns RV_OK or the first drive or cg error.
int64_t rv_board_draw(rv_pdko *pdk, const float *world, int64_t frame);

} // namespace example_cpp
