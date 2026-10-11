#pragma once

#include <cstdint>

#include "pdk/rv_pdko.h"

namespace example_cpp
{

// Draws the lettering of the factory board on the board object's panel: lit letters
// and their halo, frame is the demo clock within its period. world is the object's
// world matrix, row-major as rv_cg_put. Returns RV_OK or the first drive or cg error.
int64_t rv_board_draw(rv_pdko *pdk, const float *world, int64_t frame);

} // namespace example_cpp
