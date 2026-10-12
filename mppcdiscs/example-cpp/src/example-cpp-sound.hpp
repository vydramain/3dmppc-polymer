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

// Music slots in sound RAM: a bar plays from one slot while the next bar is written into the other.
constexpr int RV_SOUND_SLOTS = 2;

// The factory loops: both run while the camera is in the workshop.
constexpr int RV_SOUND_LOOP_COUNT = 2;

// Music, clack and loop state. Filled once by rv_sound_init; frame = -1 before the first frame.
struct rv_sound_state {
    // The last frame the sound played for, and its area and bar; the bar counts from the area span's first frame.
    int64_t frame = -1;
    rv_sound_area area = rv_sound_area::workshop;
    int64_t bar = -1;
    // Per frame of the period: its area, and the first frame of its area span (cyclic). Empty until built.
    std::vector<rv_sound_area> areas;
    std::vector<int64_t> span_start;
    // Per music slot: sound RAM address, bar asset in it (nullptr when unknown), whether its voice was started,
    // and the frame it was started at (see sounding()).
    int64_t slot_addr[RV_SOUND_SLOTS] = {};
    const char *slot_name[RV_SOUND_SLOTS] = {};
    bool slot_armed[RV_SOUND_SLOTS] = {};
    int64_t slot_frame[RV_SOUND_SLOTS] = {};
    // Per factory loop: whether its voice was started and not stopped since.
    bool loop_armed[RV_SOUND_LOOP_COUNT] = {};
    // Set once the first write-ahead failure is logged; a failure that persists would log every bar.
    bool write_ahead_logged = false;
    // One bar read from the disc, slot-sized.
    std::vector<uint8_t> scratch;
    // The asset of the last failure, for the caller's log.
    const char *error_asset = "";
};

// Measures the music on the disc, allocates the music slots, and tabulates the area and span of every frame
// of the period from keys (non-empty, sorted by index). Called once; returns RV_OK or the first drive or ca
// error, with state.error_asset naming the asset.
int64_t rv_sound_init(rv_pdko *pdk, const std::vector<rv_route_key> &keys, rv_sound_state &state);

// Plays what frame `frame` (in [0, RV_ROUTE_PERIOD_FRAMES)) needs after state.frame: the bar of its area
// at each bar start, the board clack in the workshop when the lit letters differ from state.frame's, and
// only when the scene has the board (board_present). The two factory loops run while the frame is in the
// workshop. The same frame again (a pause, a repeated render) plays nothing. The first frame fills the
// slots and waits for the next bar start when it is mid-bar.
// Needs rv_sound_init first. Returns RV_OK or the drive or ca error, with state.error_asset naming the asset.
int64_t rv_sound_update(rv_pdko *pdk, int64_t frame, bool board_present, rv_sound_state &state);

} // namespace example_cpp
