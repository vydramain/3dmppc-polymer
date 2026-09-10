#include "rv_pconsole/rv_pchost_sdl3.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/ca/rv_pcmixer.hpp"

namespace rv_3dmppc
{

namespace
{

// Radial dead zone: below this fraction of full deflection a stick reads as
// centred. Sticks rest slightly off-centre on real hardware and would otherwise
// drift a camera forever.
constexpr float RV_PCHOST_STICK_DEADZONE = 0.25f;

// A trigger past SOFT is "being pulled" (the liveness bit), past FULL is bottomed
// out — the two-stage pull the rv_isource vocabulary asks for.
constexpr float RV_PCHOST_TRIGGER_SOFT = 0.15f;
constexpr float RV_PCHOST_TRIGGER_FULL = 0.90f;

// What the keyboard overlay on port 0 is able to report. A keyboard has no
// analog anything, but the layout below drives the sticks digitally, so the
// stick sources are advertised honestly — a disc that asks "can port 0 give me
// a left stick?" gets yes, and gets ±1 values.
constexpr uint64_t RV_PCHOST_KEYBOARD_ABILITIES =
    RV_ISOURCE_FRONT_BTTN_SOUTH | RV_ISOURCE_FRONT_BTTN_EAST | RV_ISOURCE_FRONT_BTTN_WEST |
    RV_ISOURCE_FRONT_BTTN_NORTH | RV_ISOURCE_BUMPER_LEFT | RV_ISOURCE_BUMPER_RIGHT |
    RV_ISOURCE_MENU_BTTN_MENU | RV_ISOURCE_MENU_BTTN_VIEW | RV_ISOURCE_DPAD_MOVE |
    RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_EAST |
    RV_ISOURCE_LEFT_STICK_MOVE | RV_ISOURCE_LEFT_STICK_DPAD_NORTH |
    RV_ISOURCE_LEFT_STICK_DPAD_SOUTH | RV_ISOURCE_LEFT_STICK_DPAD_WEST |
    RV_ISOURCE_LEFT_STICK_DPAD_EAST | RV_ISOURCE_RIGHT_STICK_MOVE |
    RV_ISOURCE_RIGHT_STICK_DPAD_NORTH | RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH |
    RV_ISOURCE_RIGHT_STICK_DPAD_WEST | RV_ISOURCE_RIGHT_STICK_DPAD_EAST;

// THEOREM: radial dead zone with rescaling. Zeroing the raw value inside the
// dead zone alone leaves a discontinuity — the stick would jump from 0 straight
// to DEADZONE the moment it crosses the boundary. Rescaling the surviving
// magnitude from [DEADZONE, 1] back onto [0, 1] restores continuity, so slow
// pushes stay slow. The zone is RADIAL (on the vector length) and not per-axis,
// otherwise the dead region is a square and a diagonal nudge registers earlier
// than a straight one.
rv_iaxes stick_axes(float raw_x, float raw_y)
{
    const float magnitude = std::sqrt(raw_x * raw_x + raw_y * raw_y);
    if (magnitude < RV_PCHOST_STICK_DEADZONE) {
        return { 0.0f, 0.0f };
    }

    const float clamped = magnitude > 1.0f ? 1.0f : magnitude;
    const float scaled = (clamped - RV_PCHOST_STICK_DEADZONE) / (1.0f - RV_PCHOST_STICK_DEADZONE);
    const float k = scaled / magnitude;
    return { raw_x * k, raw_y * k };
}

// THEOREM: sectorization. An analog stick becomes four digital directions by
// comparing |x| against |y|: the dominant axis picks the sector, its sign picks
// the side. This is the diagonal-free 4-way reading; a disc that wants the
// diagonals reads the raw axes instead.
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

// SDL reports a stick axis as [-32768, 32767] with +y pointing DOWN. The console
// is left-handed with +y UP (see README), so the vertical axis is negated here,
// at the single point where SDL's conventions are allowed to exist.
float axis_norm(int16_t raw)
{
    return static_cast<float>(raw) / 32767.0f;
}

float trigger_norm(int16_t raw)
{
    const float v = static_cast<float>(raw) / 32767.0f;
    return v < 0.0f ? 0.0f : v;
}

// Frames the audio callback produces per pass. Small enough that the staging
// buffer is a few kilobytes, large enough that a device asking for 20 ms does
// not walk the mixer a hundred times.
constexpr int RV_PCHOST_AUDIO_BLOCK_FRAMES = 512;

// Bytes one stereo S16 frame occupies on the device side.
constexpr int RV_PCHOST_AUDIO_FRAME_BYTES = 2 * static_cast<int>(sizeof(int16_t));

} // namespace

// Out of line because rv_pcport is private — this is the only way stage E3's
// resource check can cost configure()'s ports_.assign(iport_count, ...).
int64_t rv_pchost_sdl3::port_bytes()
{
    return static_cast<int64_t>(sizeof(rv_pcport));
}

rv_pchost_sdl3::rv_pchost_sdl3()
{
}

rv_pchost_sdl3::~rv_pchost_sdl3()
{
    // The device thread goes first: everything below it is state a callback in
    // flight would be reading.
    close_audio();

    for (rv_pcport &port : ports_) {
        if (port.pad) {
            SDL_CloseGamepad(port.pad);
        }
    }
    if (window_) {
        SDL_DestroyWindow(window_);
    }
    if (video_ready_ || gamepad_ready_ || audio_ready_) {
        SDL_Quit();
    }
}

bool rv_pchost_sdl3::ensure_sdl(uint32_t flags)
{
    return SDL_InitSubSystem(flags);
}

void rv_pchost_sdl3::configure(int64_t iport_count)
{
    ports_.assign(static_cast<std::size_t>(iport_count > 0 ? iport_count : 0), rv_pcport{});

    // Adopt whatever is already plugged in; later arrivals come as events.
    // Skipped outright when the subsystem never came up — SDL_GetGamepads
    // would just report nothing, so this is only saving the call. Done here
    // rather than in open() because input must not depend on a window ever
    // opening.
    if (gamepad_ready_) {
        int pad_count = 0;
        SDL_JoystickID *pads = SDL_GetGamepads(&pad_count);
        if (pads) {
            for (int i = 0; i < pad_count; ++i) {
                adopt_gamepad(pads[i]);
            }
            SDL_free(pads);
        }
    }
}

int64_t rv_pchost_sdl3::prepare(bool want_video, bool want_gamepad, bool want_audio)
{
    if (want_video) {
        video_ready_ = ensure_sdl(SDL_INIT_VIDEO);
        if (!video_ready_) {
            RV_LOG_WARN("pchost", "SDL_INIT_VIDEO failed: {}", SDL_GetError());
        } else {
            SDL_Rect display_bounds{};
            SDL_DisplayID display = SDL_GetPrimaryDisplay();
            if (display != 0 && SDL_GetDisplayBounds(display, &display_bounds)) {
                display_width_ = display_bounds.w;
                display_height_ = display_bounds.h;
                RV_LOG_INFO("pchost", "display bounds measured at {}x{}", display_width_,
                    display_height_);
            } else {
                RV_LOG_WARN("pchost", "SDL_GetDisplayBounds failed: {}", SDL_GetError());
            }
        }
    }

    if (want_gamepad) {
        gamepad_ready_ = ensure_sdl(SDL_INIT_GAMEPAD);
        if (!gamepad_ready_) {
            RV_LOG_WARN("pchost", "SDL_INIT_GAMEPAD failed: {}, ports will read as empty",
                SDL_GetError());
        }
    }

    if (want_audio) {
        const bool subsystem_ok = ensure_sdl(SDL_INIT_AUDIO);
        const bool device_ok = subsystem_ok && open_audio_device();
        audio_ready_ = subsystem_ok && device_ok;
        if (!subsystem_ok) {
            RV_LOG_WARN("pchost", "audio subsystem did not come up: {}", SDL_GetError());
        } else if (!device_ok) {
            RV_LOG_WARN("pchost", "audio device failed to open: {}", SDL_GetError());
        }
    }

    return RV_OK;
}

int64_t rv_pchost_sdl3::open(const char *title, int64_t screen_width, int64_t screen_height,
    uint64_t scale)
{
    // Video already came up (or didn't) in prepare(), stage C. open() only
    // builds the window on top of that — it never retries bringing video up
    // itself. Gamepad adoption already happened in configure(), independent
    // of a window.
    if (!video_ready_) {
        RV_LOG_ERR("pchost", "video never came up at stage C, refusing to open a window");
        return RV_ERR_IO;
    }

    if (scale == 0) {
        scale = 1;
    }

    const int window_w = static_cast<int>(screen_width * static_cast<int64_t>(scale));
    const int window_h = static_cast<int>(screen_height * static_cast<int64_t>(scale));
    window_ = SDL_CreateWindow(title, window_w, window_h, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        RV_LOG_ERR("pchost", "SDL_CreateWindow failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    RV_LOG_INFO("pchost", "window {}x{} at scale {}, {} port slot(s), video={} gamepad={}",
        screen_width, screen_height, scale, ports_.size(), video_ready_, gamepad_ready_);
    return RV_OK;
}

void rv_pchost_sdl3::adopt_gamepad(uint32_t joystick_id)
{
    for (rv_pcport &port : ports_) {
        if (port.pad) {
            continue;
        }

        SDL_Gamepad *pad = SDL_OpenGamepad(joystick_id);
        if (!pad) {
            RV_LOG_WARN("pchost", "SDL_OpenGamepad failed: {}", SDL_GetError());
            return;
        }

        port.pad = pad;
        port.joystick_id = joystick_id;

        // Abilities are STATIC capability, asked of the device once: "does this
        // controller have this source at all", as opposed to the live bits in
        // rv_istate::buttons.
        uint64_t abilities = 0;
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
                abilities |= entry.source;
            }
        }

        if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)) {
            abilities |= RV_ISOURCE_LEFT_STICK_MOVE | RV_ISOURCE_LEFT_STICK_DPAD_NORTH |
                RV_ISOURCE_LEFT_STICK_DPAD_SOUTH | RV_ISOURCE_LEFT_STICK_DPAD_WEST |
                RV_ISOURCE_LEFT_STICK_DPAD_EAST;
        }
        if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)) {
            abilities |= RV_ISOURCE_RIGHT_STICK_MOVE | RV_ISOURCE_RIGHT_STICK_DPAD_NORTH |
                RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH | RV_ISOURCE_RIGHT_STICK_DPAD_WEST |
                RV_ISOURCE_RIGHT_STICK_DPAD_EAST;
        }
        if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) {
            abilities |= RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL | RV_ISOURCE_LEFT_TRIGGER_FULL_PULL;
        }
        if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            abilities |= RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL | RV_ISOURCE_RIGHT_TRIGGER_FULL_PULL;
        }

        port.abilities = abilities;
        RV_LOG_INFO("pchost", "gamepad '{}' adopted into port {}", SDL_GetGamepadName(pad),
            static_cast<std::size_t>(&port - ports_.data()));
        return;
    }

    RV_LOG_WARN("pchost", "every port slot is occupied, gamepad {} ignored", joystick_id);
}

void rv_pchost_sdl3::release_gamepad(uint32_t joystick_id)
{
    for (rv_pcport &port : ports_) {
        if (!port.pad || port.joystick_id != joystick_id) {
            continue;
        }

        SDL_CloseGamepad(port.pad);
        // The slot empties but never renumbers: index N stays the same player.
        port.pad = nullptr;
        port.joystick_id = 0;
        port.abilities = 0;
        port.state = rv_istate{};
        RV_LOG_INFO("pchost", "port {} emptied",
            static_cast<std::size_t>(&port - ports_.data()));
        return;
    }
}

void rv_pchost_sdl3::pump()
{
    // SDL_INIT_GAMEPAD implies the events subsystem, so the queue exists (and
    // is worth draining for arrival/departure and power-off events) even when
    // video never came up — input must not depend on a window.
    if (!video_ready_ && !gamepad_ready_) {
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            power_off_ = true;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            adopt_gamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            release_gamepad(event.gdevice.which);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            // rv_cio::imouse is variant B — motion relative to the previous
            // poll — so the deltas accumulate here and are drained on read.
            mouse_dx_ += event.motion.xrel;
            mouse_dy_ += event.motion.yrel;
            break;
        default:
            break;
        }
    }

    // PATTERN: snapshot polling. The event stream above only tracks device
    // arrival and departure; the STATE is re-read wholesale every frame, which
    // is exactly what rv_cio promises — an instantaneous level, never an edge.
    for (std::size_t i = 0; i < ports_.size(); ++i) {
        ports_[i].state = rv_istate{};
        poll_gamepad(ports_[i]);
        if (i == 0) {
            overlay_keyboard(ports_[i]);
        }
    }
}

void rv_pchost_sdl3::poll_gamepad(rv_pcport &port)
{
    SDL_Gamepad *pad = port.pad;
    if (!pad) {
        return;
    }

    rv_istate &state = port.state;

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
        // "Option" on a DualSense / Steam Deck is SDL's START. This is the bit
        // the skeleton disc watches to power itself down.
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

    state.buttons |= stick_direction_bits(
        state.left_stick, RV_ISOURCE_LEFT_STICK_DPAD_NORTH, RV_ISOURCE_LEFT_STICK_DPAD_SOUTH,
        RV_ISOURCE_LEFT_STICK_DPAD_WEST, RV_ISOURCE_LEFT_STICK_DPAD_EAST,
        RV_ISOURCE_LEFT_STICK_MOVE);
    state.buttons |= stick_direction_bits(
        state.right_stick, RV_ISOURCE_RIGHT_STICK_DPAD_NORTH, RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH,
        RV_ISOURCE_RIGHT_STICK_DPAD_WEST, RV_ISOURCE_RIGHT_STICK_DPAD_EAST,
        RV_ISOURCE_RIGHT_STICK_MOVE);

    state.left_trigger = trigger_norm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    state.right_trigger = trigger_norm(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));

    if (state.left_trigger > RV_PCHOST_TRIGGER_SOFT) {
        state.buttons |= RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL;
    }
    if (state.left_trigger > RV_PCHOST_TRIGGER_FULL) {
        state.buttons |= RV_ISOURCE_LEFT_TRIGGER_FULL_PULL;
    }
    if (state.right_trigger > RV_PCHOST_TRIGGER_SOFT) {
        state.buttons |= RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL;
    }
    if (state.right_trigger > RV_PCHOST_TRIGGER_FULL) {
        state.buttons |= RV_ISOURCE_RIGHT_TRIGGER_FULL_PULL;
    }

    // DEFERRED: gyro / accelerometer and the Steam Deck trackpads. Neither is
    // advertised in `abilities`, so a disc reading rv_imotion legitimately sees
    // zeroes rather than a lie.
}

void rv_pchost_sdl3::overlay_keyboard(rv_pcport &port)
{
    const bool *keys = SDL_GetKeyboardState(nullptr);
    if (!keys) {
        return;
    }

    // The keyboard is OVERLAID on port 0 rather than owning a port of its own:
    // a disc should not care whether the human is holding a pad or a keyboard,
    // and player one is player one either way. Bits are OR-ed, so a pad and the
    // keyboard can drive the same slot at once.
    rv_istate &state = port.state;
    port.abilities |= RV_PCHOST_KEYBOARD_ABILITIES;

    const struct {
        SDL_Scancode key;
        uint64_t source;
    } key_map[] = {
        // Esc is the console's "Option": the same bit SDL_GAMEPAD_BUTTON_START
        // raises, so the skeleton disc needs no keyboard-specific branch.
        { SDL_SCANCODE_ESCAPE, RV_ISOURCE_MENU_BTTN_MENU },
        { SDL_SCANCODE_TAB, RV_ISOURCE_MENU_BTTN_VIEW },
        { SDL_SCANCODE_SPACE, RV_ISOURCE_FRONT_BTTN_SOUTH },
        { SDL_SCANCODE_Z, RV_ISOURCE_FRONT_BTTN_SOUTH },
        { SDL_SCANCODE_X, RV_ISOURCE_FRONT_BTTN_EAST },
        { SDL_SCANCODE_C, RV_ISOURCE_FRONT_BTTN_WEST },
        { SDL_SCANCODE_V, RV_ISOURCE_FRONT_BTTN_NORTH },
        { SDL_SCANCODE_Q, RV_ISOURCE_BUMPER_LEFT },
        { SDL_SCANCODE_E, RV_ISOURCE_BUMPER_RIGHT },
        { SDL_SCANCODE_UP, RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_MOVE },
        { SDL_SCANCODE_DOWN, RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_MOVE },
        { SDL_SCANCODE_LEFT, RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_MOVE },
        { SDL_SCANCODE_RIGHT, RV_ISOURCE_DPAD_EAST | RV_ISOURCE_DPAD_MOVE },
    };
    for (const auto &entry : key_map) {
        if (keys[entry.key]) {
            state.buttons |= entry.source;
        }
    }

    // WASD drives the left stick, IJKL the right one. Digital keys can only
    // produce the corners of the square, so the values are ±1 with no dead zone
    // to apply — the stick helper would only rescale a magnitude that is already
    // saturated.
    const float wasd_x = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    const float wasd_y = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    const float ijkl_x = (keys[SDL_SCANCODE_L] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_J] ? 1.0f : 0.0f);
    const float ijkl_y = (keys[SDL_SCANCODE_I] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_K] ? 1.0f : 0.0f);

    if (wasd_x != 0.0f || wasd_y != 0.0f) {
        state.left_stick = { wasd_x, wasd_y };
        state.buttons |= stick_direction_bits(
            state.left_stick, RV_ISOURCE_LEFT_STICK_DPAD_NORTH, RV_ISOURCE_LEFT_STICK_DPAD_SOUTH,
            RV_ISOURCE_LEFT_STICK_DPAD_WEST, RV_ISOURCE_LEFT_STICK_DPAD_EAST,
            RV_ISOURCE_LEFT_STICK_MOVE);
    }
    if (ijkl_x != 0.0f || ijkl_y != 0.0f) {
        state.right_stick = { ijkl_x, ijkl_y };
        state.buttons |= stick_direction_bits(
            state.right_stick, RV_ISOURCE_RIGHT_STICK_DPAD_NORTH,
            RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH, RV_ISOURCE_RIGHT_STICK_DPAD_WEST,
            RV_ISOURCE_RIGHT_STICK_DPAD_EAST, RV_ISOURCE_RIGHT_STICK_MOVE);
    }
}

bool rv_pchost_sdl3::open_audio_device()
{
    if (audio_stream_) {
        return true;
    }

    audio_block_.assign(
        static_cast<std::size_t>(RV_PCHOST_AUDIO_BLOCK_FRAMES) * RV_PCMIXER_CHANNELS, 0);

    // The console's own format, stated once. SDL_OpenAudioDeviceStream binds a
    // converting stream to the device, so whatever rate and layout the hardware
    // actually wants is SDL's problem: the mixer always produces 44100 Hz
    // stereo and never learns otherwise. That is why the SPU has no resampler —
    // see the zero-resampling theorem in rv_pcvoice.cpp.
    //
    // SDL_AUDIO_S16 is the NATIVE-endian alias on purpose: these are int16
    // values this process computed, not bytes read off a disc. The S16LE in the
    // sound-RAM format is a different statement and lives in rv_pcvoice.cpp.
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = static_cast<int>(RV_PCMIXER_CHANNELS);
    spec.freq = static_cast<int>(RV_PCA_SAMPLE_RATE);

    audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
        &rv_pchost_sdl3::audio_stream_callback, this);
    if (!audio_stream_) {
        RV_LOG_WARN("pchost", "SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
        return false;
    }

    // Devices open paused; from here on the callback runs on SDL's thread. No
    // mixer is attached yet — fill_audio() fills silence until attach_mixer()
    // is called.
    if (!SDL_ResumeAudioStreamDevice(audio_stream_)) {
        RV_LOG_WARN("pchost", "SDL_ResumeAudioStreamDevice failed: {}", SDL_GetError());
        SDL_DestroyAudioStream(audio_stream_);
        audio_stream_ = nullptr;
        return false;
    }

    RV_LOG_INFO("pchost", "audio out at {} Hz, {} ch", RV_PCA_SAMPLE_RATE, RV_PCMIXER_CHANNELS);
    return true;
}

void rv_pchost_sdl3::attach_mixer(rv_pcmixer &mixer)
{
    if (!audio_stream_) {
        return;
    }

    // Locking the stream keeps the callback, which runs on SDL's own thread,
    // from reading audio_mixer_ mid-assignment.
    SDL_LockAudioStream(audio_stream_);
    audio_mixer_ = &mixer;
    SDL_UnlockAudioStream(audio_stream_);
}

void rv_pchost_sdl3::detach_mixer()
{
    if (!audio_stream_) {
        audio_mixer_ = nullptr;
        return;
    }

    SDL_LockAudioStream(audio_stream_);
    audio_mixer_ = nullptr;
    SDL_UnlockAudioStream(audio_stream_);
}

void rv_pchost_sdl3::close_audio()
{
    if (!audio_stream_) {
        audio_mixer_ = nullptr;
        return;
    }

    // SDL_DestroyAudioStream unbinds the stream from the device and waits for a
    // callback in flight to finish, so once it returns the mixer pointer is
    // provably unreachable and rv_pcca may destroy the mixer it points at.
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
    audio_mixer_ = nullptr;
}

void rv_pchost_sdl3::audio_stream_callback(void *userdata, SDL_AudioStream *stream,
    int additional_amount, int /*total_amount*/)
{
    rv_pchost_sdl3 *host = static_cast<rv_pchost_sdl3 *>(userdata);
    if (host) {
        host->fill_audio(stream, additional_amount);
    }
}

void rv_pchost_sdl3::fill_audio(SDL_AudioStream *stream, int wanted_bytes)
{
    if (!stream || wanted_bytes <= 0) {
        return;
    }

    // `wanted_bytes` is expressed in the DEVICE's format, which after SDL's
    // conversion need not be ours; rounding up by our own frame size only ever
    // hands the stream a little more than it asked for, which it buffers.
    int frames_left =
        (wanted_bytes + RV_PCHOST_AUDIO_FRAME_BYTES - 1) / RV_PCHOST_AUDIO_FRAME_BYTES;

    while (frames_left > 0) {
        const int block =
            frames_left < RV_PCHOST_AUDIO_BLOCK_FRAMES ? frames_left : RV_PCHOST_AUDIO_BLOCK_FRAMES;

        // No mixer attached (device came up before any disc did, or the disc
        // runs with ca=null): the device stays alive and clocked, it just gets
        // silence instead of the SPU's output.
        if (audio_mixer_) {
            audio_mixer_->render(audio_block_.data(), block);
        } else {
            std::fill_n(audio_block_.data(), static_cast<std::size_t>(block) * RV_PCMIXER_CHANNELS,
                static_cast<int16_t>(0));
        }
        if (!SDL_PutAudioStreamData(stream, audio_block_.data(),
                block * RV_PCHOST_AUDIO_FRAME_BYTES)) {
            // Losing the queue mid-callback is not worth spinning over; the
            // device will ask again and the next block starts a frame later.
            return;
        }

        frames_left -= block;
    }
}

const rv_istate &rv_pchost_sdl3::port_state(int64_t port) const
{
    if (port < 0 || static_cast<std::size_t>(port) >= ports_.size()) {
        return empty_state_;
    }
    return ports_[static_cast<std::size_t>(port)].state;
}

uint64_t rv_pchost_sdl3::port_abilities(int64_t port) const
{
    if (port < 0 || static_cast<std::size_t>(port) >= ports_.size()) {
        return 0;
    }
    return ports_[static_cast<std::size_t>(port)].abilities;
}

rv_imouse rv_pchost_sdl3::consume_mouse()
{
    const rv_imouse motion{ static_cast<int>(mouse_dx_), static_cast<int>(mouse_dy_) };
    mouse_dx_ = 0.0f;
    mouse_dy_ = 0.0f;
    return motion;
}

int64_t rv_pchost_sdl3::rumble(int64_t port, uint16_t left, uint16_t right, uint16_t duration_ms)
{
    if (port < 0 || static_cast<std::size_t>(port) >= ports_.size()) {
        return RV_ERR_INVAL;
    }

    SDL_Gamepad *pad = ports_[static_cast<std::size_t>(port)].pad;
    if (!pad) {
        return RV_ERR_INVAL;
    }

    return SDL_RumbleGamepad(pad, left, right, duration_ms) ? RV_OK : RV_ERR_IO;
}

int64_t rv_pchost_sdl3::rumble_triggers(int64_t port, uint16_t left, uint16_t right,
    uint16_t duration_ms)
{
    if (port < 0 || static_cast<std::size_t>(port) >= ports_.size()) {
        return RV_ERR_INVAL;
    }

    SDL_Gamepad *pad = ports_[static_cast<std::size_t>(port)].pad;
    if (!pad) {
        return RV_ERR_INVAL;
    }

    return SDL_RumbleGamepadTriggers(pad, left, right, duration_ms) ? RV_OK : RV_ERR_IO;
}

} // namespace rv_3dmppc
