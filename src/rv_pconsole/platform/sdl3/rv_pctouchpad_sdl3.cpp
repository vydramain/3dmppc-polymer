// The web on-screen gamepad: a virtual SDL joystick showing the groups the disc asks for. Native has no pad.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace rv_3dmppc
{
namespace
{
// The pad the rv_pctouchpad_* C functions drive: they have no object to reach it by, and there is one pad per process.
rv_pctouch_pad_sdl3 *live_touch_pad = nullptr;

#ifdef __EMSCRIPTEN__
using rv_pdklib::rv_touch_group;

// Triggers are axes SDL always gives a pad, and gyro is not a button: neither has a row here.
struct rv_pctouch_group_row {
    rv_touch_group group;
    uint32_t buttons;
};

constexpr uint32_t rv_pctouch_button(SDL_GamepadButton button)
{
    return 1u << static_cast<int>(button);
}

constexpr rv_pctouch_group_row RV_PCTOUCH_GROUP_ROWS[] = {
    // MISC3 and MISC4 are the stick touch sensors, as rv_pcgamepads_sdl3.cpp reads them.
    { rv_touch_group::sticks,
        rv_pctouch_button(SDL_GAMEPAD_BUTTON_LEFT_STICK) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_RIGHT_STICK) |
            rv_pctouch_button(SDL_GAMEPAD_BUTTON_MISC3) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_MISC4) },
    // TOUCHPAD is the left trackpad's click, MISC2 the right one's (rv_pcgamepads_sdl3_touch.cpp).
    { rv_touch_group::trackpads, rv_pctouch_button(SDL_GAMEPAD_BUTTON_TOUCHPAD) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_MISC2) },
    { rv_touch_group::dpad,
        rv_pctouch_button(SDL_GAMEPAD_BUTTON_DPAD_UP) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
            rv_pctouch_button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) },
    { rv_touch_group::face,
        rv_pctouch_button(SDL_GAMEPAD_BUTTON_SOUTH) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_EAST) |
            rv_pctouch_button(SDL_GAMEPAD_BUTTON_WEST) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_NORTH) },
    { rv_touch_group::bumpers,
        rv_pctouch_button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) },
    { rv_touch_group::rear,
        rv_pctouch_button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2) |
            rv_pctouch_button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2) },
    { rv_touch_group::menu, rv_pctouch_button(SDL_GAMEPAD_BUTTON_BACK) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_START) },
};

bool touch_group_shown(uint32_t groups, rv_touch_group group)
{
    return (groups & (1u << static_cast<unsigned>(group))) != 0;
}

uint32_t touch_buttons(uint32_t groups)
{
    uint32_t buttons = 0;
    for (const auto &row : RV_PCTOUCH_GROUP_ROWS) {
        if (touch_group_shown(groups, row.group)) {
            buttons |= row.buttons;
        }
    }
    return buttons;
}

// The page's name for its pad: SDL shows it in the log.
constexpr const char *RV_PCTOUCH_PAD_NAME = "3dmppc on-screen gamepad";

// One finger per trackpad: the page draws each as a single touch point.
constexpr uint16_t RV_PCTOUCH_PAD_FINGERS = 1;
constexpr SDL_VirtualJoystickTouchpadDesc RV_PCTOUCH_PAD_TRACKPADS[RV_PCGAMEPADS_SDL3_TRACKPADS] = {
    { .nfingers = RV_PCTOUCH_PAD_FINGERS, .padding = {} },
    { .nfingers = RV_PCTOUCH_PAD_FINGERS, .padding = {} },
};

// Assumption, not measured: the page sends no motion rate, so 60 Hz is a guess to check on a phone.
constexpr float RV_PCTOUCH_MOTION_RATE_HZ = 60.0f;
constexpr SDL_VirtualJoystickSensorDesc RV_PCTOUCH_SENSORS[] = {
    { .type = SDL_SENSOR_GYRO, .rate = RV_PCTOUCH_MOTION_RATE_HZ },
    { .type = SDL_SENSOR_ACCEL, .rate = RV_PCTOUCH_MOTION_RATE_HZ },
};
// x, y, z of one sensor sample.
constexpr int RV_PCTOUCH_VEC_VALUES = 3;

// Page fact set by src/web/rv_web_touchpad.js, which the web pre-js bundle includes; undefined reads as false.
bool page_touchpad_wanted()
{
    return EM_ASM_INT({ return Module.rvTouchpadWanted ? 1 : 0; });
}

// Ceiling: navigator.vibrate stops by itself after this long. SDL sends zero strength when the disc's rumble ends,
// which cancels it sooner. A rumble with no end (duration 0 in SDL_RumbleGamepad) gets no zero from SDL, so it stops
// here. Upgrade: a keep-alive that re-sends the vibration before this ends, once a disc needs a rumble longer than this.
constexpr int RV_PCTOUCH_VIBRATE_MS = 60000;

// A browser gives one strength (Android) or none (iOS): any non-zero strength vibrates, zero stops it.
bool SDLCALL page_rumble(void *, Uint16 low, Uint16 high)
{
    const int duration = (low != 0 || high != 0) ? RV_PCTOUCH_VIBRATE_MS : 0;
    const int vibrates = EM_ASM_INT(
        {
            if (!navigator.vibrate) {
                return 0;
            }
            navigator.vibrate($0);
            return 1;
        },
        duration);
    if (vibrates == 0) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            RV_LOG_INFO("pcplatform", "the browser has no navigator.vibrate: the disc's rumble is not felt here");
        }
        return SDL_Unsupported();
    }
    return true;
}

// The page calls before show_touch_pad, or with no pad: one warning per process, not one per call.
int no_touch_pad()
{
    static bool warned = false;
    if (!warned) {
        warned = true;
        RV_LOG_WARN("pcplatform", "rv_pctouchpad_*: no on-screen gamepad is attached");
    }
    return RV_ERR_INVAL;
}

// The page sends motion to a pad with no gyro: one warning per process, as no_touch_pad().
int no_touch_motion()
{
    static bool warned = false;
    if (!warned) {
        warned = true;
        RV_LOG_WARN("pcplatform", "rv_pctouchpad_motion: the on-screen gamepad has no motion sensors");
    }
    return RV_ERR_INVAL;
}

// SDL numbers the pad's buttons by the set bits of its mask, in bit order. -1 when the mask has no such button.
int touch_button_index(uint32_t mask, int button)
{
    static_assert(SDL_GAMEPAD_BUTTON_COUNT <= 32, "the button mask is 32 bits");
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT || (mask & (1u << button)) == 0) {
        return -1;
    }
    return std::popcount(mask & ((1u << button) - 1));
}

// Full scale in SDL's Sint16 units; SDL's stick y grows down, the console's up.
constexpr float RV_PCTOUCH_AXIS_MAX = static_cast<float>(SDL_JOYSTICK_AXIS_MAX);
constexpr float RV_PCTOUCH_STICK_Y_SIGN = -1.0f;
// The page draws one touch point per trackpad, so each pad is driven with its first finger.
constexpr int RV_PCTOUCH_FINGER = 0;
#endif
} // namespace

rv_pctouch_pad_sdl3::~rv_pctouch_pad_sdl3()
{
    // Order: the pointer clears first, so the page's calls get RV_ERR_INVAL; then the handle closes; then the device detaches.
    if (live_touch_pad == this) {
        live_touch_pad = nullptr;
    }
    if (joystick != nullptr) {
        SDL_CloseJoystick(joystick);
    }
    // id stays 0 when nothing was attached; otherwise SDL is still up, the pads are closed.
    if (id != 0 && !SDL_DetachVirtualJoystick(id)) {
        RV_LOG_WARN("pcplatform", "SDL_DetachVirtualJoystick failed: {}", SDL_GetError());
    }
}

#if defined(__EMSCRIPTEN__)
int rv_pcplatform_sdl3::show_touch_pad(uint32_t groups)
{
    if (touch_pad_.id != 0) {
        RV_LOG_WARN("pcplatform", "show_touch_pad called again: the on-screen gamepad is already attached");
        return RV_ERR_INVAL;
    }
    if (!page_touchpad_wanted()) {
        return RV_OK;
    }

    // axis_mask 0: SDL fills all six axes for a gamepad-type device, so an axis index is its SDL_GamepadAxis.
    // nbuttons is the mask's bit count: SDL gives each set bit the next index and drops bits past nbuttons.
    const uint32_t buttons = touch_buttons(groups);
    const bool trackpads = touch_group_shown(groups, rv_touch_group::trackpads);
    const bool motion = touch_group_shown(groups, rv_touch_group::gyro);
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = static_cast<uint16_t>(std::popcount(buttons));
    desc.button_mask = buttons;
    desc.axis_mask = 0;
    desc.ntouchpads = trackpads ? RV_PCGAMEPADS_SDL3_TRACKPADS : 0;
    desc.touchpads = trackpads ? RV_PCTOUCH_PAD_TRACKPADS : nullptr;
    desc.nsensors = motion ? static_cast<uint16_t>(SDL_arraysize(RV_PCTOUCH_SENSORS)) : 0;
    desc.sensors = motion ? RV_PCTOUCH_SENSORS : nullptr;
    desc.Rumble = page_rumble;
    desc.vendor_id = RV_PCTOUCH_PAD_VENDOR_ID;
    desc.product_id = RV_PCTOUCH_PAD_PRODUCT_ID;
    desc.name = RV_PCTOUCH_PAD_NAME;

    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        RV_LOG_WARN("pcplatform", "SDL_AttachVirtualJoystick failed, the on-screen gamepad is off: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    touch_pad_.id = id;
    touch_pad_.button_mask = buttons;
    touch_pad_.motion = motion;
    touch_pad_.joystick = SDL_OpenJoystick(id);
    if (touch_pad_.joystick == nullptr) {
        RV_LOG_WARN("pcplatform", "SDL_OpenJoystick failed, the on-screen gamepad is off: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    live_touch_pad = &touch_pad_;
    RV_LOG_INFO("pcplatform", "on-screen gamepad: {} buttons, {} trackpads, motion {}", desc.nbuttons, desc.ntouchpads, motion);

    const int delivered = EM_ASM_INT(
        {
            if (typeof Module.rvTouchpadShow != 'function') {
                return 0;
            }
            Module.rvTouchpadShow($0);
            return 1;
        },
        groups);
    if (delivered == 0) {
        RV_LOG_WARN("pcplatform", "the page has no rvTouchpadShow: the on-screen gamepad is attached but not shown");
    }
    return RV_OK;
}

// The page's calls, Module._rv_pctouchpad_*: each returns RV_OK or RV_ERR_*.
// down is an int from the page: a bool argument other than 0 or 1 is undefined.
extern "C" EMSCRIPTEN_KEEPALIVE int rv_pctouchpad_button(int button, int down)
{
    if (live_touch_pad == nullptr) {
        return no_touch_pad();
    }
    const int index = touch_button_index(live_touch_pad->button_mask, button);
    if (index < 0) {
        RV_LOG_WARN("pcplatform", "rv_pctouchpad_button: button {} is not on the pad", button);
        return RV_ERR_INVAL;
    }
    if (!SDL_SetJoystickVirtualButton(live_touch_pad->joystick, index, down != 0)) {
        RV_LOG_WARN("pcplatform", "SDL_SetJoystickVirtualButton failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    return RV_OK;
}

extern "C" EMSCRIPTEN_KEEPALIVE int rv_pctouchpad_axis(int axis, float value)
{
    if (live_touch_pad == nullptr) {
        return no_touch_pad();
    }
    const bool trigger = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    const bool stick_y = axis == SDL_GAMEPAD_AXIS_LEFTY || axis == SDL_GAMEPAD_AXIS_RIGHTY;
    const bool known = axis >= SDL_GAMEPAD_AXIS_LEFTX && axis < SDL_GAMEPAD_AXIS_COUNT;
    if (!known || !std::isfinite(value)) {
        RV_LOG_WARN("pcplatform", "rv_pctouchpad_axis: axis {} value {} is not a pad axis", axis, value);
        return RV_ERR_INVAL;
    }
    // Sticks are -1..1, triggers 0..1; values past the range are cut off at it.
    const float clamped = std::clamp(value, trigger ? 0.0f : -1.0f, 1.0f);
    const float sign = stick_y ? RV_PCTOUCH_STICK_Y_SIGN : 1.0f;
    const auto raw = static_cast<Sint16>(std::lround(sign * clamped * RV_PCTOUCH_AXIS_MAX));
    if (!SDL_SetJoystickVirtualAxis(live_touch_pad->joystick, axis, raw)) {
        RV_LOG_WARN("pcplatform", "SDL_SetJoystickVirtualAxis failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    return RV_OK;
}

extern "C" EMSCRIPTEN_KEEPALIVE int rv_pctouchpad_trackpad(int pad, int down, float x, float y)
{
    if (live_touch_pad == nullptr) {
        return no_touch_pad();
    }
    const bool known = pad >= 0 && pad < RV_PCGAMEPADS_SDL3_TRACKPADS && std::isfinite(x) && std::isfinite(y);
    if (!known) {
        RV_LOG_WARN("pcplatform", "rv_pctouchpad_trackpad: pad {} at {}, {} is not a trackpad point", pad, x, y);
        return RV_ERR_INVAL;
    }
    // Points are 0..1 from the top left corner, as SDL has them; the pressure says whether a finger is down.
    const float px = std::clamp(x, 0.0f, 1.0f);
    const float py = std::clamp(y, 0.0f, 1.0f);
    const bool is_down = down != 0;
    const float pressure = is_down ? 1.0f : 0.0f;
    if (!SDL_SetJoystickVirtualTouchpad(live_touch_pad->joystick, pad, RV_PCTOUCH_FINGER, is_down, px, py, pressure)) {
        RV_LOG_WARN("pcplatform", "SDL_SetJoystickVirtualTouchpad failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    return RV_OK;
}

// The page's motion sample in SDL's units: gyro rad/s, accel m/s^2 (the page converts the axes).
// Both sensors take one timestamp, in nanoseconds, from the same SDL clock.
extern "C" EMSCRIPTEN_KEEPALIVE int rv_pctouchpad_motion(float gx, float gy, float gz, float ax, float ay, float az)
{
    if (live_touch_pad == nullptr) {
        return no_touch_pad();
    }
    if (!live_touch_pad->motion) {
        return no_touch_motion();
    }
    const bool gyro_finite = std::isfinite(gx) && std::isfinite(gy) && std::isfinite(gz);
    const bool accel_finite = std::isfinite(ax) && std::isfinite(ay) && std::isfinite(az);
    if (!gyro_finite || !accel_finite) {
        RV_LOG_WARN("pcplatform", "rv_pctouchpad_motion: a sample value is not finite");
        return RV_ERR_INVAL;
    }
    const float gyro[RV_PCTOUCH_VEC_VALUES] = { gx, gy, gz };
    const float accel[RV_PCTOUCH_VEC_VALUES] = { ax, ay, az };
    const uint64_t stamp = SDL_GetTicksNS();
    const bool sent =
        SDL_SendJoystickVirtualSensorData(live_touch_pad->joystick, SDL_SENSOR_GYRO, stamp, gyro, RV_PCTOUCH_VEC_VALUES) &&
        SDL_SendJoystickVirtualSensorData(live_touch_pad->joystick, SDL_SENSOR_ACCEL, stamp, accel, RV_PCTOUCH_VEC_VALUES);
    if (!sent) {
        RV_LOG_WARN("pcplatform", "SDL_SendJoystickVirtualSensorData failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    return RV_OK;
}
#endif

} // namespace rv_3dmppc
