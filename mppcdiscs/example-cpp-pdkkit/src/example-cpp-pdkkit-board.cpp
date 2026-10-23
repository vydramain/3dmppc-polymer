#include "example-cpp-pdkkit-board.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace example_cpp_pdkkit
{

namespace
{

// The log tag of the wear warning.
constexpr const char *RV_FLICKER_TAG = "flicker";

// Shifts of Marsaglia's xorshift32 (13, 17, 5): a full-period generator of nonzero 32-bit states, as in the sound's clack.
constexpr int RV_XORSHIFT_LEFT_1 = 13;
constexpr int RV_XORSHIFT_RIGHT = 17;
constexpr int RV_XORSHIFT_LEFT_2 = 5;

// A frame is 1/60 s; the times below are counted in frames, and in seconds through this rate.
constexpr int32_t RV_FLICKER_FRAMES_PER_SECOND = 60;
constexpr int32_t RV_FLICKER_TENTHS_PER_SECOND = 10;
// A steady burn lasts 3 to 12 s, as an old sign's lamp does.
constexpr int32_t RV_FLICKER_STEADY_MIN = 3 * RV_FLICKER_FRAMES_PER_SECOND;
constexpr int32_t RV_FLICKER_STEADY_MAX = 12 * RV_FLICKER_FRAMES_PER_SECOND;
// Wear 0..100 divides the steady burn by 1 + wear / 25 in integers, so the divisor steps 1..5 every 25 of wear:
// a lamp at 100 burns a fifth as long.
constexpr int32_t RV_FLICKER_WEAR_MAX = 100;
constexpr int32_t RV_FLICKER_WEAR_STEP = 25;
// A dark lamp stays out 0.4 to 2 s: 4 and 20 tenths of a second.
constexpr int32_t RV_FLICKER_DARK_MIN = 4 * RV_FLICKER_FRAMES_PER_SECOND / RV_FLICKER_TENTHS_PER_SECOND;
constexpr int32_t RV_FLICKER_DARK_MAX = 20 * RV_FLICKER_FRAMES_PER_SECOND / RV_FLICKER_TENTHS_PER_SECOND;
// A contact bounces 2 to 6 frames (33 to 100 ms) apart between switches.
constexpr int32_t RV_FLICKER_GAP_MIN = 2;
constexpr int32_t RV_FLICKER_GAP_MAX = 6;
// Switch counts of a bounce. Its parity sets where it ends: from lit, an even count ends lit, an odd count ends dark.
// A bounce that ends steady takes an even 4..8; one that ends dark takes an odd 3..9; a bounce from dark that ends lit
// takes 1..3.
constexpr int32_t RV_FLICKER_LIT_BOUNCE_MIN = 4;
constexpr int32_t RV_FLICKER_LIT_BOUNCE_MAX = 8;
constexpr int32_t RV_FLICKER_DARK_BOUNCE_MIN = 3;
constexpr int32_t RV_FLICKER_DARK_BOUNCE_MAX = 9;
constexpr int32_t RV_FLICKER_ON_BOUNCE_MIN = 1;
constexpr int32_t RV_FLICKER_ON_BOUNCE_MAX = 3;
// The draw of a bounce's outcome is 0..99; the share of bounces that end steady is 70 of those 100.
constexpr int32_t RV_FLICKER_PERCENT = 100;
constexpr int32_t RV_FLICKER_STEADY_PERCENT = 70;
// Spreads the seeds of nearby cells apart; a product of 0 becomes 1, as xorshift32 never leaves 0.
constexpr uint32_t RV_FLICKER_SEED_MIX = 2654435761u;

// Holders of T, listed first: a component cannot be added or removed inside ecs_component_each.
template <typename T>
std::vector<rv_pdkkit::ecs_id> holders_of(rv_pdkkit::ecs_world &world)
{
    std::vector<rv_pdkkit::ecs_id> holders;
    rv_pdkkit::ecs_component_each<T>(world, [&holders](rv_pdkkit::ecs_id id, T &) {
        holders.push_back(id);
    });
    return holders;
}

// Removes last frame's flicker_switched; the event lives one frame.
int clear_switched(rv_pdkkit::ecs_world &world)
{
    const std::vector<rv_pdkkit::ecs_id> holders = holders_of<ecs_component_flicker_switched>(world);
    for (const rv_pdkkit::ecs_id id : holders) {
        const int remove_code = rv_pdkkit::ecs_component_remove<ecs_component_flicker_switched>(world, id);
        if (remove_code != RV_OK) {
            return remove_code;
        }
    }
    return RV_OK;
}

// xorshift32 step: the state never becomes 0, so the sequence never stops.
uint32_t next_random(uint32_t &seed)
{
    seed ^= seed << RV_XORSHIFT_LEFT_1;
    seed ^= seed >> RV_XORSHIFT_RIGHT;
    seed ^= seed << RV_XORSHIFT_LEFT_2;
    return seed;
}

// Uniform in lo..hi, both included.
int32_t uniform(uint32_t &seed, int32_t lo, int32_t hi)
{
    const uint32_t span = static_cast<uint32_t>(hi - lo + 1);
    return lo + static_cast<int32_t>(next_random(seed) % span);
}

// A switch count from lo to hi in steps of 2: lo and hi have the parity the bounce needs.
int32_t bounce_switches(uint32_t &seed, int32_t lo, int32_t hi)
{
    return lo + 2 * uniform(seed, 0, (hi - lo) / 2);
}

// Switch count of the bounce that a steady burn starts: 70% end steady, the rest end dark.
int32_t steady_bounce_switches(uint32_t &seed)
{
    const bool ends_steady = uniform(seed, 0, RV_FLICKER_PERCENT - 1) < RV_FLICKER_STEADY_PERCENT;
    return ends_steady ? bounce_switches(seed, RV_FLICKER_LIT_BOUNCE_MIN, RV_FLICKER_LIT_BOUNCE_MAX) :
                         bounce_switches(seed, RV_FLICKER_DARK_BOUNCE_MIN, RV_FLICKER_DARK_BOUNCE_MAX);
}

// Frames of a steady burn for this wear, or of a dark spell: the phase a bounce has ended in.
int32_t phase_frames(uint32_t &seed, flicker_phase phase, int32_t wear)
{
    if (phase == flicker_phase::steady) {
        const int32_t frames = uniform(seed, RV_FLICKER_STEADY_MIN, RV_FLICKER_STEADY_MAX);
        return frames / (1 + wear / RV_FLICKER_WEAR_STEP);
    }
    return uniform(seed, RV_FLICKER_DARK_MIN, RV_FLICKER_DARK_MAX);
}

// The lamp's wear clamped to 0..100. Out of range is warned once per lamp; the scene's value is left as it is.
int32_t wear_of(ecs_component_flicker &flicker)
{
    const bool in_range = flicker.wear >= 0 && flicker.wear <= RV_FLICKER_WEAR_MAX;
    if (!in_range && !flicker.wear_warned) {
        flicker.wear_warned = true;
        RV_LOG_WARN(RV_FLICKER_TAG,
            "flicker cell {}: wear {} is outside 0..{}, clamped",
            flicker.cell,
            flicker.wear,
            RV_FLICKER_WEAR_MAX);
    }
    return std::clamp(flicker.wear, 0, RV_FLICKER_WEAR_MAX);
}

// First frame of a lamp: it burns, its seed comes from cell.
void start_lamp(ecs_component_flicker &flicker, int32_t wear)
{
    const uint32_t mixed = static_cast<uint32_t>(flicker.cell) * RV_FLICKER_SEED_MIX;
    flicker.seed = mixed != 0 ? mixed : 1u;
    flicker.lit = true;
    flicker.phase = flicker_phase::steady;
    flicker.frames_left = phase_frames(flicker.seed, flicker_phase::steady, wear);
    flicker.inited = true;
}

// One frame of the lamp's cycle. Returns true on a frame where lit switches.
bool step_cycle(ecs_component_flicker &flicker, int32_t wear)
{
    // frames_left counts down to the next event: the event is on the frame it reaches 0.
    if (--flicker.frames_left > 0) {
        return false;
    }
    switch (flicker.phase) {
    case flicker_phase::steady:
        flicker.switches_left = steady_bounce_switches(flicker.seed);
        break;
    case flicker_phase::dark:
        flicker.switches_left = bounce_switches(flicker.seed, RV_FLICKER_ON_BOUNCE_MIN, RV_FLICKER_ON_BOUNCE_MAX);
        break;
    case flicker_phase::bounce:
        // The bounce goes on: its switches are already counted.
        break;
    }
    flicker.phase = flicker_phase::bounce;
    flicker.lit = !flicker.lit;
    --flicker.switches_left;
    // The last switch ends the bounce on its target: lit burns on, dark stays out.
    const bool bounce_over = flicker.switches_left == 0;
    const flicker_phase next = flicker.lit ? flicker_phase::steady : flicker_phase::dark;
    flicker.phase = bounce_over ? next : flicker_phase::bounce;
    flicker.frames_left =
        bounce_over ? phase_frames(flicker.seed, next, wear) : uniform(flicker.seed, RV_FLICKER_GAP_MIN, RV_FLICKER_GAP_MAX);
    return true;
}

// Shows the lamp by removing its hidden mark, or hides it by adding one. A lamp with no mark is already shown: NOENT is fine.
int set_lit(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, bool lit)
{
    if (lit) {
        const int shown_code = rv_pdkkit::ecs_component_remove<rv_pdkkit::ecs_component_hidden>(world, id);
        const bool no_mark = shown_code == RV_ERR_NOENT;
        if (shown_code != RV_OK && !no_mark) {
            return shown_code;
        }
        return RV_OK;
    }
    rv_pdkkit::ecs_component_hidden *hidden = nullptr;
    const int hidden_code = rv_pdkkit::ecs_component_ensure<rv_pdkkit::ecs_component_hidden>(world, id, hidden);
    if (hidden_code != RV_OK) {
        return hidden_code;
    }
    return RV_OK;
}

} // namespace

int ecs_system_flicker(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &)
{
    const int clear_code = clear_switched(world);
    if (clear_code != RV_OK) {
        return clear_code;
    }
    // Listed first: the hidden and event marks are added and removed in the walk below.
    const std::vector<rv_pdkkit::ecs_id> lamps = holders_of<ecs_component_flicker>(world);
    for (const rv_pdkkit::ecs_id id : lamps) {
        ecs_component_flicker *flicker = rv_pdkkit::ecs_component_get<ecs_component_flicker>(world, id);
        const int32_t wear = wear_of(*flicker);
        if (!flicker->inited) {
            start_lamp(*flicker, wear);
            const int first_code = set_lit(world, id, true);
            if (first_code != RV_OK) {
                return first_code;
            }
            continue;
        }
        if (!step_cycle(*flicker, wear)) {
            continue;
        }
        const bool lit = flicker->lit;
        const int lit_code = set_lit(world, id, lit);
        if (lit_code != RV_OK) {
            return lit_code;
        }
        ecs_component_flicker_switched *switched = nullptr;
        const int switched_code = rv_pdkkit::ecs_component_ensure<ecs_component_flicker_switched>(world, id, switched);
        if (switched_code != RV_OK) {
            return switched_code;
        }
    }
    return RV_OK;
}

} // namespace example_cpp_pdkkit
