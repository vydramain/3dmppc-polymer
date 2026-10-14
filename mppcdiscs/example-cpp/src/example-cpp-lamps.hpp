#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdk/rv_pdko.h"
#include "example-cpp-route.hpp"

namespace example_cpp
{

// A lamp of the scene (lamp_N): its world matrix, row-major as rv_cg_put, and the frames of the
// route period in which it burns, [on_frame, off_frame). Both 0 means it never burns.
struct rv_lamp {
    std::string name;
    float world[16] = {};
    int64_t on_frame = 0;
    int64_t off_frame = 0;
};

// True when name is "lamp_" followed by at least one character.
bool rv_lamp_name(const char *name);

// Sets on_frame and off_frame of each lamp from the route keys (sorted by index). A lamp the route
// does not pass in both halves of the period stays dark and gets one warning.
void rv_lamps_schedule(const std::vector<rv_route_key> &keys, std::vector<rv_lamp> &lamps);

// Puts the light model of each lamp that burns at frame (within the period). Returns RV_OK or the
// first drive or cg error.
int64_t rv_lamps_draw(rv_pdko *pdk, const std::vector<rv_lamp> &lamps, int64_t frame);

} // namespace example_cpp
