// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — хост SDL, презентация кадра и снимки контроллеров.
// ──────────────────────────────────────────────────────────────────────────────
#include "rv_pconsole/rv_pchost.hpp"

#include <SDL3/SDL.h>

#include <cmath>

#include "pdk/rv_err.hpp"
#include "rv_infra/rv_log.hpp"

namespace rv_3dmppc {
namespace {

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
rv_iaxes stick_axes(float raw_x, float raw_y) {
    const float magnitude = std::sqrt(raw_x * raw_x + raw_y * raw_y);
    if (magnitude < RV_PCHOST_STICK_DEADZONE) return {0.0f, 0.0f};

    const float clamped = magnitude > 1.0f ? 1.0f : magnitude;
    const float scaled = (clamped - RV_PCHOST_STICK_DEADZONE) / (1.0f - RV_PCHOST_STICK_DEADZONE);
    const float k = scaled / magnitude;
    return {raw_x * k, raw_y * k};
}

// THEOREM: sectorization. An analog stick becomes four digital directions by
// comparing |x| against |y|: the dominant axis picks the sector, its sign picks
// the side. This is the diagonal-free 4-way reading; a disc that wants the
// diagonals reads the raw axes instead.
uint64_t stick_direction_bits(rv_iaxes axes, uint64_t north, uint64_t south, uint64_t west,
                              uint64_t east, uint64_t move) {
    if (axes.x == 0.0f && axes.y == 0.0f) return 0;

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
float axis_norm(int16_t raw) { return static_cast<float>(raw) / 32767.0f; }

float trigger_norm(int16_t raw) {
    const float v = static_cast<float>(raw) / 32767.0f;
    return v < 0.0f ? 0.0f : v;
}

}  // namespace

rv_pchost::rv_pchost(const rv_pconsole_conf& conf)
    : screen_width_(conf.cv.screen_width),
      screen_height_(conf.cv.screen_height),
      ports_(static_cast<std::size_t>(conf.cio.iport_count > 0 ? conf.cio.iport_count : 0)) {}

rv_pchost::~rv_pchost() {
    for (rv_pcport& port : ports_) {
        if (port.pad) SDL_CloseGamepad(port.pad);
    }
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (sdl_ready_) SDL_Quit();
}

int64_t rv_pchost::open(const char* title, uint64_t scale) {
    if (scale == 0) scale = 1;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        RV_LOG_ERR("pchost", "SDL_Init failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    sdl_ready_ = true;

    const int window_w = static_cast<int>(screen_width_ * static_cast<int64_t>(scale));
    const int window_h = static_cast<int>(screen_height_ * static_cast<int64_t>(scale));
    window_ = SDL_CreateWindow(title, window_w, window_h, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        RV_LOG_ERR("pchost", "SDL_CreateWindow failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        RV_LOG_ERR("pchost", "SDL_CreateRenderer failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    // The frame is always the native console resolution; the window is just a
    // magnifying glass over it. Integer scaling keeps the pixels square and
    // crisp instead of smearing them across a non-multiple window size.
    SDL_SetRenderLogicalPresentation(renderer_, static_cast<int>(screen_width_),
                                     static_cast<int>(screen_height_),
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                 static_cast<int>(screen_width_),
                                 static_cast<int>(screen_height_));
    if (!texture_) {
        RV_LOG_ERR("pchost", "SDL_CreateTexture failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);

    // Adopt whatever is already plugged in; later arrivals come as events.
    int pad_count = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&pad_count);
    if (pads) {
        for (int i = 0; i < pad_count; ++i) adopt_gamepad(pads[i]);
        SDL_free(pads);
    }

    RV_LOG_INFO("pchost", "display {}x{} at scale {}, {} port slot(s)", screen_width_,
                screen_height_, scale, ports_.size());
    return RV_OK;
}

void rv_pchost::adopt_gamepad(uint32_t joystick_id) {
    for (rv_pcport& port : ports_) {
        if (port.pad) continue;

        SDL_Gamepad* pad = SDL_OpenGamepad(joystick_id);
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
            {SDL_GAMEPAD_BUTTON_SOUTH, RV_ISOURCE_FRONT_BTTN_SOUTH},
            {SDL_GAMEPAD_BUTTON_EAST, RV_ISOURCE_FRONT_BTTN_EAST},
            {SDL_GAMEPAD_BUTTON_WEST, RV_ISOURCE_FRONT_BTTN_WEST},
            {SDL_GAMEPAD_BUTTON_NORTH, RV_ISOURCE_FRONT_BTTN_NORTH},
            {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, RV_ISOURCE_BUMPER_LEFT},
            {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RV_ISOURCE_BUMPER_RIGHT},
            {SDL_GAMEPAD_BUTTON_START, RV_ISOURCE_MENU_BTTN_MENU},
            {SDL_GAMEPAD_BUTTON_BACK, RV_ISOURCE_MENU_BTTN_VIEW},
            {SDL_GAMEPAD_BUTTON_LEFT_STICK, RV_ISOURCE_LEFT_STICK_CLICK},
            {SDL_GAMEPAD_BUTTON_RIGHT_STICK, RV_ISOURCE_RIGHT_STICK_CLICK},
            {SDL_GAMEPAD_BUTTON_DPAD_UP, RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_MOVE},
            {SDL_GAMEPAD_BUTTON_DPAD_DOWN, RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_MOVE},
            {SDL_GAMEPAD_BUTTON_DPAD_LEFT, RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_MOVE},
            {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, RV_ISOURCE_DPAD_EAST | RV_ISOURCE_DPAD_MOVE},
        };
        for (const auto& entry : button_map) {
            if (SDL_GamepadHasButton(pad, entry.button)) abilities |= entry.source;
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

void rv_pchost::release_gamepad(uint32_t joystick_id) {
    for (rv_pcport& port : ports_) {
        if (!port.pad || port.joystick_id != joystick_id) continue;

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

void rv_pchost::pump() {
    if (!sdl_ready_) return;

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
        if (i == 0) overlay_keyboard(ports_[i]);
    }
}

void rv_pchost::poll_gamepad(rv_pcport& port) {
    SDL_Gamepad* pad = port.pad;
    if (!pad) return;

    rv_istate& state = port.state;

    const struct {
        SDL_GamepadButton button;
        uint64_t source;
    } live_map[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, RV_ISOURCE_FRONT_BTTN_SOUTH},
        {SDL_GAMEPAD_BUTTON_EAST, RV_ISOURCE_FRONT_BTTN_EAST},
        {SDL_GAMEPAD_BUTTON_WEST, RV_ISOURCE_FRONT_BTTN_WEST},
        {SDL_GAMEPAD_BUTTON_NORTH, RV_ISOURCE_FRONT_BTTN_NORTH},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, RV_ISOURCE_BUMPER_LEFT},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RV_ISOURCE_BUMPER_RIGHT},
        // "Option" on a DualSense / Steam Deck is SDL's START. This is the bit
        // the skeleton disc watches to power itself down.
        {SDL_GAMEPAD_BUTTON_START, RV_ISOURCE_MENU_BTTN_MENU},
        {SDL_GAMEPAD_BUTTON_BACK, RV_ISOURCE_MENU_BTTN_VIEW},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, RV_ISOURCE_LEFT_STICK_CLICK},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, RV_ISOURCE_RIGHT_STICK_CLICK},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_MOVE},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_MOVE},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_MOVE},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, RV_ISOURCE_DPAD_EAST | RV_ISOURCE_DPAD_MOVE},
    };
    for (const auto& entry : live_map) {
        if (SDL_GetGamepadButton(pad, entry.button)) state.buttons |= entry.source;
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

void rv_pchost::overlay_keyboard(rv_pcport& port) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (!keys) return;

    // The keyboard is OVERLAID on port 0 rather than owning a port of its own:
    // a disc should not care whether the human is holding a pad or a keyboard,
    // and player one is player one either way. Bits are OR-ed, so a pad and the
    // keyboard can drive the same slot at once.
    rv_istate& state = port.state;
    port.abilities |= RV_PCHOST_KEYBOARD_ABILITIES;

    const struct {
        SDL_Scancode key;
        uint64_t source;
    } key_map[] = {
        // Esc is the console's "Option": the same bit SDL_GAMEPAD_BUTTON_START
        // raises, so the skeleton disc needs no keyboard-specific branch.
        {SDL_SCANCODE_ESCAPE, RV_ISOURCE_MENU_BTTN_MENU},
        {SDL_SCANCODE_TAB, RV_ISOURCE_MENU_BTTN_VIEW},
        {SDL_SCANCODE_SPACE, RV_ISOURCE_FRONT_BTTN_SOUTH},
        {SDL_SCANCODE_Z, RV_ISOURCE_FRONT_BTTN_SOUTH},
        {SDL_SCANCODE_X, RV_ISOURCE_FRONT_BTTN_EAST},
        {SDL_SCANCODE_C, RV_ISOURCE_FRONT_BTTN_WEST},
        {SDL_SCANCODE_V, RV_ISOURCE_FRONT_BTTN_NORTH},
        {SDL_SCANCODE_Q, RV_ISOURCE_BUMPER_LEFT},
        {SDL_SCANCODE_E, RV_ISOURCE_BUMPER_RIGHT},
        {SDL_SCANCODE_UP, RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_MOVE},
        {SDL_SCANCODE_DOWN, RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_MOVE},
        {SDL_SCANCODE_LEFT, RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_MOVE},
        {SDL_SCANCODE_RIGHT, RV_ISOURCE_DPAD_EAST | RV_ISOURCE_DPAD_MOVE},
    };
    for (const auto& entry : key_map) {
        if (keys[entry.key]) state.buttons |= entry.source;
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
        state.left_stick = {wasd_x, wasd_y};
        state.buttons |= stick_direction_bits(
            state.left_stick, RV_ISOURCE_LEFT_STICK_DPAD_NORTH, RV_ISOURCE_LEFT_STICK_DPAD_SOUTH,
            RV_ISOURCE_LEFT_STICK_DPAD_WEST, RV_ISOURCE_LEFT_STICK_DPAD_EAST,
            RV_ISOURCE_LEFT_STICK_MOVE);
    }
    if (ijkl_x != 0.0f || ijkl_y != 0.0f) {
        state.right_stick = {ijkl_x, ijkl_y};
        state.buttons |= stick_direction_bits(
            state.right_stick, RV_ISOURCE_RIGHT_STICK_DPAD_NORTH,
            RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH, RV_ISOURCE_RIGHT_STICK_DPAD_WEST,
            RV_ISOURCE_RIGHT_STICK_DPAD_EAST, RV_ISOURCE_RIGHT_STICK_MOVE);
    }
}

void rv_pchost::present(const uint32_t* argb) {
    if (!renderer_ || !texture_ || !argb) return;

    SDL_UpdateTexture(texture_, nullptr, argb, static_cast<int>(screen_width_ * 4));
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

const rv_istate& rv_pchost::port_state(int64_t port) const {
    if (port < 0 || static_cast<std::size_t>(port) >= ports_.size()) return empty_state_;
    return ports_[static_cast<std::size_t>(port)].state;
}

uint64_t rv_pchost::port_abilities(int64_t port) const {
    if (port < 0 || static_cast<std::size_t>(port) >= ports_.size()) return 0;
    return ports_[static_cast<std::size_t>(port)].abilities;
}

rv_imouse rv_pchost::consume_mouse() {
    const rv_imouse motion{static_cast<int>(mouse_dx_), static_cast<int>(mouse_dy_)};
    mouse_dx_ = 0.0f;
    mouse_dy_ = 0.0f;
    return motion;
}

int64_t rv_pchost::rumble(int64_t port, uint16_t left, uint16_t right, uint16_t duration_ms) {
    if (port < 0 || static_cast<std::size_t>(port) >= ports_.size()) return RV_ERR_INVAL;

    SDL_Gamepad* pad = ports_[static_cast<std::size_t>(port)].pad;
    if (!pad) return RV_ERR_INVAL;

    return SDL_RumbleGamepad(pad, left, right, duration_ms) ? RV_OK : RV_ERR_IO;
}

int64_t rv_pchost::rumble_triggers(int64_t port, uint16_t left, uint16_t right,
                                   uint16_t duration_ms) {
    if (port < 0 || static_cast<std::size_t>(port) >= ports_.size()) return RV_ERR_INVAL;

    SDL_Gamepad* pad = ports_[static_cast<std::size_t>(port)].pad;
    if (!pad) return RV_ERR_INVAL;

    return SDL_RumbleGamepadTriggers(pad, left, right, duration_ms) ? RV_OK : RV_ERR_IO;
}

}  // namespace rv_3dmppc
