#pragma once

#include <cstdint>

#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"

namespace example_cpp_pdkkit
{

// Where a lamp is in its cycle: burns steady, bounces on its contact, or is dark.
enum class flicker_phase : uint8_t {
    steady,
    bounce,
    dark
};

// A lamp of the board: a halo or a letter. Scene fields: cell is the lamp's seed, wear 0..100 is how worn it is (a worn lamp
// burns shorter). The rest is the working state of the flicker system, not scene fields.
struct ecs_component_flicker {
    int32_t cell = 0;
    int32_t wear = 0;
    // Lit now; each switch sets it, with the hidden mark.
    bool lit = true;
    // False until the first frame: the first frame lights the lamp with no event.
    bool inited = false;
    // Set once a wear outside 0..100 is warned, so the warning comes once.
    bool wear_warned = false;
    flicker_phase phase = flicker_phase::steady;
    // Frames until the next switch or phase end.
    int32_t frames_left = 0;
    // Switches still to make in the current bounce.
    int32_t switches_left = 0;
    // xorshift32 state, seeded from cell; never 0.
    uint32_t seed = 0;
};

// One-frame event on a lamp that switched: the flicker system sets it and removes it at the start of its next pass.
struct ecs_component_flicker_switched {};

// Frame system: removes last frame's flicker_switched, then steps each lamp's own cycle; every switch shows or hides the lamp
// and sets its flicker_switched. Error: the code of the first step that fails (NOENT, NOMEM); the rest of the frame is skipped.
// A refusal of the hidden mark leaves the lamp's state ahead of the mark until its next switch; a refusal of
// flicker_switched loses that switch's event.
// Not logged here: ecs_frame_report logs the name and code.
int ecs_system_flicker(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_flicker,
    "flicker",
    PDKKIT_FIELD(cell, int32, 0),
    PDKKIT_FIELD(wear, int32, 0));
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_flicker_switched, "flicker_switched");
