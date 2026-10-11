#pragma once

#include <cstdint>
#include <vector>

#include "pdk/rv_pdko.h"
#include "example-cpp-route.hpp"

namespace example_cpp
{

// The place the camera is in: the factory (workshop), the street, the flat (apartment).
enum class rv_sound_area : int {
    workshop = 0,
    street = 1,
    flat = 2,
};

// The last frame the sound played for, and the area its music belongs to. frame = -1 before the first frame.
struct rv_sound_state {
    int64_t frame = -1;
    rv_sound_area area = rv_sound_area::workshop;
};

// Plays what frame `frame` (in [0, RV_ROUTE_PERIOD_FRAMES)) needs after state.frame: the music of the
// area on the first frame and at each change of area; the board clack in the workshop when the lit
// letters differ from state.frame's, and only when the scene has the board (board_present). The same
// frame again (a pause, a repeated render) plays nothing. keys as for rv_route_camera_at, non-empty.
// Returns RV_OK or the drive or ca error.
int64_t
rv_sound_update(rv_pdko *pdk, const std::vector<rv_route_key> &keys, int64_t frame, bool board_present, rv_sound_state &state);

} // namespace example_cpp
