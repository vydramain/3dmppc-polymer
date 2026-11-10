// The web on-screen gamepad: a virtual SDL joystick showing the groups the disc asks for. Native has no pad.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include <bit>

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
    { rv_touch_group::sticks,
        rv_pctouch_button(SDL_GAMEPAD_BUTTON_LEFT_STICK) | rv_pctouch_button(SDL_GAMEPAD_BUTTON_RIGHT_STICK) },
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

// The page's name for its pad: SDL shows it in the log. A virtual device has no USB ids.
constexpr const char *RV_PCTOUCH_PAD_NAME = "3dmppc on-screen gamepad";

// One finger per trackpad: the page draws each as a single touch point.
constexpr uint16_t RV_PCTOUCH_PAD_FINGERS = 1;
constexpr SDL_VirtualJoystickTouchpadDesc RV_PCTOUCH_PAD_TRACKPADS[RV_PCGAMEPADS_SDL3_TRACKPADS] = {
    { .nfingers = RV_PCTOUCH_PAD_FINGERS, .padding = {} },
    { .nfingers = RV_PCTOUCH_PAD_FINGERS, .padding = {} },
};

// Page fact set by src/web/rv_web_touchpad.js, which the web pre-js bundle includes; undefined reads as false.
bool page_touchpad_wanted()
{
    return EM_ASM_INT({ return Module.rvTouchpadWanted ? 1 : 0; });
}
#endif
} // namespace

rv_pctouch_pad_sdl3::~rv_pctouch_pad_sdl3()
{
    // id stays 0 when nothing was attached; otherwise SDL is still up, the pads are closed.
    if (id != 0 && !SDL_DetachVirtualJoystick(id)) {
        RV_LOG_WARN("pcplatform", "SDL_DetachVirtualJoystick failed: {}", SDL_GetError());
    }
}

#if defined(__EMSCRIPTEN__)
int rv_pcplatform_sdl3::show_touch_pad(uint32_t groups)
{
    if (!page_touchpad_wanted()) {
        return RV_OK;
    }

    // SDL fills both sticks and both triggers itself: axis_mask stays 0. nbuttons is the mask's bit count: SDL gives
    // each set bit the next index and drops bits past nbuttons.
    const uint32_t buttons = touch_buttons(groups);
    const bool trackpads = touch_group_shown(groups, rv_touch_group::trackpads);
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = static_cast<uint16_t>(std::popcount(buttons));
    desc.button_mask = buttons;
    desc.axis_mask = 0;
    desc.ntouchpads = trackpads ? RV_PCGAMEPADS_SDL3_TRACKPADS : 0;
    desc.touchpads = trackpads ? RV_PCTOUCH_PAD_TRACKPADS : nullptr;
    desc.name = RV_PCTOUCH_PAD_NAME;

    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        RV_LOG_WARN("pcplatform", "SDL_AttachVirtualJoystick failed, the on-screen gamepad is off: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    touch_pad_.id = id;

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
#endif

} // namespace rv_3dmppc
