// The SDL3 gamepads: which pads are connected, their static abilities, and
// the per-frame live snapshot. Which virtual port a pad drives is cio's job,
// not this class's - every pad SDL reports is opened here.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include <cmath>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

// Radial dead zone: below this fraction of full deflection a stick reads as
// centred. Sticks rest slightly off-centre on real hardware and would
// otherwise drift a camera forever.
constexpr float RV_PCGAMEPADS_SDL3_STICK_DEADZONE = 0.25f;

// A trigger past SOFT is "being pulled" (the liveness bit), past FULL is
// bottomed out - the two-stage pull the rv_isource vocabulary asks for.
constexpr float RV_PCGAMEPADS_SDL3_TRIGGER_SOFT = 0.15f;
constexpr float RV_PCGAMEPADS_SDL3_TRIGGER_FULL = 0.90f;

rv_iaxes stick_axes(float raw_x, float raw_y)
{
    const float magnitude = std::sqrt(raw_x * raw_x + raw_y * raw_y);
    if (magnitude < RV_PCGAMEPADS_SDL3_STICK_DEADZONE) {
        return { 0.0f, 0.0f };
    }

    const float clamped = magnitude > 1.0f ? 1.0f : magnitude;
    const float scaled = (clamped - RV_PCGAMEPADS_SDL3_STICK_DEADZONE) /
        (1.0f - RV_PCGAMEPADS_SDL3_STICK_DEADZONE);
    const float k = scaled / magnitude;
    return { raw_x * k, raw_y * k };
}

uint64_t stick_direction_bits(rv_iaxes axes, uint64_t north, uint64_t south, uint64_t west,
    uint64_t east, uint64_t move)
{
    if (axes.x == 0.0f && axes.y == 0.0f) {
        return 0;
    }

    uint64_t bits = move;
    if (std::fabs(axes.x) > std::fabs(axes.y)) {
        bits |= axes.x > 0.0f ? east : west;
    } else {
        bits |= axes.y > 0.0f ? north : south;
    }
    return bits;
}

// SDL reports a stick axis as [-32768, 32767] with +y pointing DOWN. The
// console is left-handed with +y UP, so the vertical axis is negated at the
// single point where SDL's conventions are allowed to exist (the caller).
float axis_norm(int16_t raw)
{
    return static_cast<float>(raw) / 32767.0f;
}

float trigger_norm(int16_t raw)
{
    const float v = static_cast<float>(raw) / 32767.0f;
    return v < 0.0f ? 0.0f : v;
}

} // namespace

rv_pcgamepads_sdl3::~rv_pcgamepads_sdl3()
{
    for (auto &entry : pads_) {
        if (entry.second.pad) {
            SDL_CloseGamepad(entry.second.pad);
        }
    }
}

uint64_t rv_pcgamepads_sdl3::generation() const
{
    return generation_;
}

const std::vector<uint32_t> &rv_pcgamepads_sdl3::connected() const
{
    return connected_;
}

uint64_t rv_pcgamepads_sdl3::abilities(uint32_t id) const
{
    const auto it = pads_.find(id);
    return it == pads_.end() ? 0 : it->second.abilities;
}

rv_istate rv_pcgamepads_sdl3::state(uint32_t id) const
{
    const auto it = pads_.find(id);
    return it == pads_.end() ? empty_state_ : it->second.state;
}

void rv_pcgamepads_sdl3::add(uint32_t joystick_id)
{
    if (pads_.count(joystick_id)) {
        return;
    }

    SDL_Gamepad *pad = SDL_OpenGamepad(joystick_id);
    if (!pad) {
        RV_LOG_WARN("pcplatform", "SDL_OpenGamepad failed: {}", SDL_GetError());
        return;
    }

    // Abilities are STATIC capability, asked of the device once: "does this
    // controller have this source at all", as opposed to the live bits in
    // rv_istate::buttons.
    uint64_t pad_abilities = 0;
    const struct {
        SDL_GamepadButton button;
        uint64_t source;
    } button_map[] = {
        { SDL_GAMEPAD_BUTTON_SOUTH, RV_ISOURCE_FRONT_BTTN_SOUTH },
        { SDL_GAMEPAD_BUTTON_EAST, RV_ISOURCE_FRONT_BTTN_EAST },
        { SDL_GAMEPAD_BUTTON_WEST, RV_ISOURCE_FRONT_BTTN_WEST },
        { SDL_GAMEPAD_BUTTON_NORTH, RV_ISOURCE_FRONT_BTTN_NORTH },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, RV_ISOURCE_BUMPER_LEFT },
        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RV_ISOURCE_BUMPER_RIGHT },
        { SDL_GAMEPAD_BUTTON_START, RV_ISOURCE_MENU_BTTN_MENU },
        { SDL_GAMEPAD_BUTTON_BACK, RV_ISOURCE_MENU_BTTN_VIEW },
        { SDL_GAMEPAD_BUTTON_LEFT_STICK, RV_ISOURCE_LEFT_STICK_CLICK },
        { SDL_GAMEPAD_BUTTON_RIGHT_STICK, RV_ISOURCE_RIGHT_STICK_CLICK },
        { SDL_GAMEPAD_BUTTON_DPAD_UP, RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_MOVE },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN, RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_MOVE },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT, RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_MOVE },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, RV_ISOURCE_DPAD_EAST | RV_ISOURCE_DPAD_MOVE },
    };
    for (const auto &entry : button_map) {
        if (SDL_GamepadHasButton(pad, entry.button)) {
            pad_abilities |= entry.source;
        }
    }

    if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)) {
        pad_abilities |= RV_ISOURCE_LEFT_STICK_MOVE | RV_ISOURCE_LEFT_STICK_DPAD_NORTH |
            RV_ISOURCE_LEFT_STICK_DPAD_SOUTH | RV_ISOURCE_LEFT_STICK_DPAD_WEST |
            RV_ISOURCE_LEFT_STICK_DPAD_EAST;
    }
    if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)) {
        pad_abilities |= RV_ISOURCE_RIGHT_STICK_MOVE | RV_ISOURCE_RIGHT_STICK_DPAD_NORTH |
            RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH | RV_ISOURCE_RIGHT_STICK_DPAD_WEST |
            RV_ISOURCE_RIGHT_STICK_DPAD_EAST;
    }
    if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) {
        pad_abilities |= RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL | RV_ISOURCE_LEFT_TRIGGER_FULL_PULL;
    }
    if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        pad_abilities |= RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL | RV_ISOURCE_RIGHT_TRIGGER_FULL_PULL;
    }

    rv_pcpad_sdl3 entry;
    entry.pad = pad;
    entry.abilities = pad_abilities;
    RV_LOG_INFO("pcplatform", "gamepad '{}' connected (id {})", SDL_GetGamepadName(pad),
        joystick_id);
    pads_.emplace(joystick_id, entry);
    connected_.push_back(joystick_id);
    ++generation_;
}

void rv_pcgamepads_sdl3::remove(uint32_t joystick_id)
{
    const auto it = pads_.find(joystick_id);
    if (it == pads_.end()) {
        return;
    }

    if (it->second.pad) {
        SDL_CloseGamepad(it->second.pad);
    }
    pads_.erase(it);

    for (auto conn_it = connected_.begin(); conn_it != connected_.end(); ++conn_it) {
        if (*conn_it == joystick_id) {
            connected_.erase(conn_it);
            break;
        }
    }

    RV_LOG_INFO("pcplatform", "gamepad {} disconnected", joystick_id);
    ++generation_;
}

void rv_pcgamepads_sdl3::poll_all()
{
    for (auto &kv : pads_) {
        SDL_Gamepad *pad = kv.second.pad;
        if (!pad) {
            continue;
        }

        rv_istate &state = kv.second.state;
        state = rv_istate{};

        const struct {
            SDL_GamepadButton button;
            uint64_t source;
        } live_map[] = {
            { SDL_GAMEPAD_BUTTON_SOUTH, RV_ISOURCE_FRONT_BTTN_SOUTH },
            { SDL_GAMEPAD_BUTTON_EAST, RV_ISOURCE_FRONT_BTTN_EAST },
            { SDL_GAMEPAD_BUTTON_WEST, RV_ISOURCE_FRONT_BTTN_WEST },
            { SDL_GAMEPAD_BUTTON_NORTH, RV_ISOURCE_FRONT_BTTN_NORTH },
            { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, RV_ISOURCE_BUMPER_LEFT },
            { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RV_ISOURCE_BUMPER_RIGHT },
            // "Option" on a DualSense / Steam Deck is SDL's START. This is the
            // bit the skeleton disc watches to power itself down.
            { SDL_GAMEPAD_BUTTON_START, RV_ISOURCE_MENU_BTTN_MENU },
            { SDL_GAMEPAD_BUTTON_BACK, RV_ISOURCE_MENU_BTTN_VIEW },
            { SDL_GAMEPAD_BUTTON_LEFT_STICK, RV_ISOURCE_LEFT_STICK_CLICK },
            { SDL_GAMEPAD_BUTTON_RIGHT_STICK, RV_ISOURCE_RIGHT_STICK_CLICK },
            { SDL_GAMEPAD_BUTTON_DPAD_UP, RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_MOVE },
            { SDL_GAMEPAD_BUTTON_DPAD_DOWN, RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_MOVE },
            { SDL_GAMEPAD_BUTTON_DPAD_LEFT, RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_MOVE },
            { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, RV_ISOURCE_DPAD_EAST | RV_ISOURCE_DPAD_MOVE },
        };
        for (const auto &entry : live_map) {
            if (SDL_GetGamepadButton(pad, entry.button)) {
                state.buttons |= entry.source;
            }
        }

        state.left_stick = stick_axes(axis_norm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)),
            -axis_norm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY)));
        state.right_stick = stick_axes(axis_norm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)),
            -axis_norm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY)));

        state.buttons |= stick_direction_bits(state.left_stick, RV_ISOURCE_LEFT_STICK_DPAD_NORTH,
            RV_ISOURCE_LEFT_STICK_DPAD_SOUTH, RV_ISOURCE_LEFT_STICK_DPAD_WEST,
            RV_ISOURCE_LEFT_STICK_DPAD_EAST, RV_ISOURCE_LEFT_STICK_MOVE);
        state.buttons |= stick_direction_bits(state.right_stick, RV_ISOURCE_RIGHT_STICK_DPAD_NORTH,
            RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH, RV_ISOURCE_RIGHT_STICK_DPAD_WEST,
            RV_ISOURCE_RIGHT_STICK_DPAD_EAST, RV_ISOURCE_RIGHT_STICK_MOVE);

        state.left_trigger = trigger_norm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        state.right_trigger =
            trigger_norm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));

        if (state.left_trigger > RV_PCGAMEPADS_SDL3_TRIGGER_SOFT) {
            state.buttons |= RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL;
        }
        if (state.left_trigger > RV_PCGAMEPADS_SDL3_TRIGGER_FULL) {
            state.buttons |= RV_ISOURCE_LEFT_TRIGGER_FULL_PULL;
        }
        if (state.right_trigger > RV_PCGAMEPADS_SDL3_TRIGGER_SOFT) {
            state.buttons |= RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL;
        }
        if (state.right_trigger > RV_PCGAMEPADS_SDL3_TRIGGER_FULL) {
            state.buttons |= RV_ISOURCE_RIGHT_TRIGGER_FULL_PULL;
        }

        // DEFERRED: gyro / accelerometer and the Steam Deck trackpads. Neither
        // is advertised in `abilities`, so a disc reading rv_imotion
        // legitimately sees zeroes rather than a lie.
    }
}

int64_t rv_pcgamepads_sdl3::rumble(uint32_t id, uint16_t left, uint16_t right,
    uint16_t duration_ms)
{
    const auto it = pads_.find(id);
    if (it == pads_.end() || !it->second.pad) {
        return RV_ERR_INVAL;
    }

    return SDL_RumbleGamepad(it->second.pad, left, right, duration_ms) ? RV_OK : RV_ERR_IO;
}

int64_t rv_pcgamepads_sdl3::rumble_triggers(uint32_t id, uint16_t left, uint16_t right,
    uint16_t duration_ms)
{
    const auto it = pads_.find(id);
    if (it == pads_.end() || !it->second.pad) {
        return RV_ERR_INVAL;
    }

    return SDL_RumbleGamepadTriggers(it->second.pad, left, right, duration_ms) ? RV_OK
                                                                                : RV_ERR_IO;
}

} // namespace rv_3dmppc
