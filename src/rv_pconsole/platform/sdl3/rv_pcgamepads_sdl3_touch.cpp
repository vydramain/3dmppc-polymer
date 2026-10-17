// The SDL3 gamepads' trackpads: abilities, and per poll the finger position, touch, click,
// quarter and swipe bits. SDL's [0, 1] with y down becomes the console's [-1, 1] with y up.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include <cmath>

#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

// Finger step per poll, in [-1, 1] units, that counts as SWIPE. Less is the drift and
// jitter of a resting finger.
constexpr float RV_PCGAMEPADS_SDL3_SWIPE_STEP = 0.08f;

struct rv_ptrackpad_bits {
    uint64_t touch;
    uint64_t swipe;
    uint64_t click;
    uint64_t north;
    uint64_t south;
    uint64_t west;
    uint64_t east;
};

struct rv_ptrackpad_row {
    SDL_GamepadButton click;
    rv_iaxes rv_istate::*position;
    rv_ptrackpad_bits bits;
};

// Index is SDL's touchpad number. The Steam Controller and Steam Deck pair puts its right
// pad's click on MISC2; a single pad (DualSense) clicks on TOUCHPAD, which is the left one.
const rv_ptrackpad_row trackpad_rows[RV_PCGAMEPADS_SDL3_TRACKPADS] = {
    { SDL_GAMEPAD_BUTTON_TOUCHPAD,
        &rv_istate::left_trackpad,
        { RV_ISOURCE_LEFT_TRACKPAD_TOUCH,
            RV_ISOURCE_LEFT_TRACKPAD_SWIPE,
            RV_ISOURCE_LEFT_TRACKPAD_CLICK,
            RV_ISOURCE_LEFT_TRACKPAD_DPAD_NORTH,
            RV_ISOURCE_LEFT_TRACKPAD_DPAD_SOUTH,
            RV_ISOURCE_LEFT_TRACKPAD_DPAD_WEST,
            RV_ISOURCE_LEFT_TRACKPAD_DPAD_EAST } },
    { SDL_GAMEPAD_BUTTON_MISC2,
        &rv_istate::right_trackpad,
        { RV_ISOURCE_RIGHT_TRACKPAD_TOUCH,
            RV_ISOURCE_RIGHT_TRACKPAD_SWIPE,
            RV_ISOURCE_RIGHT_TRACKPAD_CLICK,
            RV_ISOURCE_RIGHT_TRACKPAD_DPAD_NORTH,
            RV_ISOURCE_RIGHT_TRACKPAD_DPAD_SOUTH,
            RV_ISOURCE_RIGHT_TRACKPAD_DPAD_WEST,
            RV_ISOURCE_RIGHT_TRACKPAD_DPAD_EAST } },
};

// Pads with more touchpads than the table are read for the first table-size ones only.
int trackpads_in_use(SDL_Gamepad *pad)
{
    const int count = SDL_GetNumGamepadTouchpads(pad);
    return count < RV_PCGAMEPADS_SDL3_TRACKPADS ? count : RV_PCGAMEPADS_SDL3_TRACKPADS;
}

// Reads one finger into console axes and returns whether it touches. A failed read is logged
// with SDL's reason and reads as up.
bool read_finger(SDL_Gamepad *pad, int touchpad, int finger, rv_iaxes &at)
{
    bool down = false;
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 0.0f;
    if (!SDL_GetGamepadTouchpadFinger(pad, touchpad, finger, &down, &x, &y, &pressure)) {
        RV_LOG_WARN("pcplatform", "trackpad {} finger {} not read: {}", touchpad, finger, SDL_GetError());
        return false;
    }
    at = rv_iaxes{ 2.0f * x - 1.0f, 1.0f - 2.0f * y };
    return down;
}

// The first finger that touches this trackpad, with its place in `at`; -1 when none does.
int touching_finger(SDL_Gamepad *pad, int touchpad, rv_iaxes &at)
{
    const int fingers = SDL_GetNumGamepadTouchpadFingers(pad, touchpad);
    for (int finger = 0; finger < fingers; ++finger) {
        if (read_finger(pad, touchpad, finger, at)) {
            return finger;
        }
    }
    return -1;
}

} // namespace

uint64_t rv_pcgamepads_sdl3_trackpad_abilities(SDL_Gamepad *pad)
{
    uint64_t abilities = 0;
    for (int i = 0; i < trackpads_in_use(pad); ++i) {
        const rv_ptrackpad_row &row = trackpad_rows[i];
        abilities |= row.bits.touch | row.bits.swipe | row.bits.north | row.bits.south | row.bits.west | row.bits.east;
        if (SDL_GamepadHasButton(pad, row.click)) {
            abilities |= row.bits.click;
        }
    }
    return abilities;
}

void rv_pcgamepads_sdl3_poll_trackpads(SDL_Gamepad *pad,
    rv_istate &state,
    rv_pctrackpad_sdl3 (&last)[RV_PCGAMEPADS_SDL3_TRACKPADS])
{
    for (int i = 0; i < trackpads_in_use(pad); ++i) {
        const rv_ptrackpad_row &row = trackpad_rows[i];

        rv_iaxes at{};
        const int finger = touching_finger(pad, i, at);
        const bool down = finger >= 0;
        const rv_iaxes now = down ? at : rv_iaxes{ 0.0f, 0.0f };
        rv_pctrackpad_sdl3 &track = last[i];
        const float step = std::hypot(now.x - track.last.x, now.y - track.last.y);
        // A different finger is not a swipe: its first step has no earlier place of its own.
        const bool swipe = down && track.touching && track.finger == finger && step > RV_PCGAMEPADS_SDL3_SWIPE_STEP;
        track.touching = down;
        track.finger = finger;
        track.last = now;

        state.*row.position = now;
        const uint64_t touch_bits = down ? row.bits.touch : 0;
        const uint64_t swipe_bits = swipe ? row.bits.swipe : 0;
        const uint64_t click_bits = SDL_GetGamepadButton(pad, row.click) ? row.bits.click : 0;
        const rv_iaxes quarter_axes = rv_pcgamepads_sdl3_stick_axes(now.x, now.y);
        const uint64_t quarter_bits = rv_pcgamepads_sdl3_stick_direction_bits(quarter_axes,
            row.bits.north,
            row.bits.south,
            row.bits.west,
            row.bits.east,
            0);
        state.buttons |= touch_bits | swipe_bits | click_bits | quarter_bits;
    }
}

} // namespace rv_3dmppc
