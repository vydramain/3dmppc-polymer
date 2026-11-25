// The SDL3 gamepads' opening: what a newly connected pad can do (parts, motion sensors,
// stick touch) and the warnings for parts its type has but SDL does not show.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include <string>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/platform/sdl3/rv_pcgamepad_parts.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace rv_3dmppc
{

namespace
{

// Yes or no: does the pad have the gyro and accel pair the filter needs.
bool has_motion_sensors(SDL_Gamepad *pad)
{
    return SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO) && SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL);
}

#ifdef __EMSCRIPTEN__
// Set on Module before main() by src/web/rv_web_pre.js.
bool page_webhid_usable()
{
    return EM_ASM_INT({ return Module.rvWebhidUsable ? 1 : 0; });
}

// The text of the page's gamepad button, or "" if there is none; cut at a UTF-8 char start like rv_webhid_product_name.
std::string page_pads_button_label()
{
    char buf[64];
    const int n = EM_ASM_INT(
        {
            const el = document.getElementById('pads');
            const bytes = new TextEncoder().encode(el ? el.textContent.trim() : "");
            let n = Math.min(bytes.length, $1 - 1);
            while (n > 0 && (bytes[n] & 0xC0) == 0x80) {
                n--;
            }
            HEAPU8.set(bytes.subarray(0, n), $0);
            HEAPU8[$0 + n] = 0;
            return n;
        },
        buf,
        static_cast<int>(sizeof(buf)));
    return std::string(buf, n);
}
#endif

// One warning for a pad that should have gyro or touchpad by its type but shows none of it to SDL.
void warn_missing_parts(SDL_Gamepad *pad, unsigned missing)
{
    const char *name = SDL_GetGamepadName(pad);
    if (!name) {
        name = "";
    }
    const char *parts = rv_pcpad_parts_text(missing);
#ifdef __EMSCRIPTEN__
    if (!page_webhid_usable()) {
        RV_LOG_WARN("pcplatform",
            "gamepad '{}': this browser does not pass its {}; Chrome or Edge 137 or newer on a computer reads them",
            name,
            parts);
        return;
    }
    const std::string label = page_pads_button_label();
    if (label.empty()) {
        RV_LOG_WARN("pcplatform",
            "gamepad '{}': this browser does not pass its {}; the page's gamepad button gives them",
            name,
            parts);
    } else {
        RV_LOG_WARN("pcplatform",
            "gamepad '{}': this browser does not pass its {}; the \"{}\" button gives them",
            name,
            parts,
            label);
    }
#else
    RV_LOG_WARN("pcplatform", "gamepad '{}': SDL gives no {} for it", name, parts);
#endif
}

// Turns on the gyro and accel pair. If SDL refuses either one, both are turned off again:
// otherwise SDL keeps sending events for a pad that declares no motion.
int enable_motion_sensors(SDL_Gamepad *pad)
{
    const bool gyro_on = SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
    const bool accel_on = SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, true);
    if (!gyro_on || !accel_on) {
        RV_LOG_WARN("pcplatform", "gamepad motion sensors refused: {}, no motion abilities", SDL_GetError());
        SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, false);
        SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, false);
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Stick touch is MISC3 (left) and MISC4 (right). On the Valve pads listed here SDL maps them so: the Steam Deck
// and the Steam Controller of the Triton family (SDL 3.4 SDL_gamepad.c, controller_list.h). On other pads MISC3
// and MISC4 mean something else (a GameCube trigger click, for one), so they are read as stick touch only for a
// vendor and product listed here. The web on-screen gamepad is listed too: its MISC3 and MISC4 are stick touch by
// the sticks group mask of its own (rv_pctouchpad_sdl3.cpp), not by an SDL map.
struct rv_pcstick_touch_pad {
    uint16_t vendor;
    uint16_t product;
};
constexpr rv_pcstick_touch_pad RV_PCGAMEPADS_SDL3_STICK_TOUCH_PADS[] = {
    { 0x28de, 0x1205 }, // Valve Steam Deck built-in controller
    { 0x28de, 0x1302 }, // Valve Steam Triton controller
    { 0x28de, 0x1303 }, // Valve Steam Triton controller (BLE)
    { 0x28de, 0x1304 }, // Valve Steam Proteus dongle
    { 0x28de, 0x1305 }, // Valve Steam Nereid dongle
    // The web on-screen gamepad: its MISC3 and MISC4 are stick touch by the sticks group mask (rv_pctouchpad_sdl3.cpp).
    { RV_PCTOUCH_PAD_VENDOR_ID, RV_PCTOUCH_PAD_PRODUCT_ID },
};

// Yes or no: are this pad's MISC3 and MISC4 its stick touch sensors.
bool has_stick_touch_buttons(SDL_Gamepad *pad)
{
    const uint16_t vendor = SDL_GetGamepadVendor(pad);
    const uint16_t product = SDL_GetGamepadProduct(pad);
    for (const auto &known : RV_PCGAMEPADS_SDL3_STICK_TOUCH_PADS) {
        if (known.vendor == vendor && known.product == product) {
            return true;
        }
    }
    return false;
}

} // namespace

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
        { SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, RV_ISOURCE_REAR_BTTN_RIGHT_UPPER },
        { SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, RV_ISOURCE_REAR_BTTN_LEFT_UPPER },
        { SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, RV_ISOURCE_REAR_BTTN_RIGHT_LOWER },
        { SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, RV_ISOURCE_REAR_BTTN_LEFT_LOWER },
    };
    for (const auto &entry : button_map) {
        if (SDL_GamepadHasButton(pad, entry.button)) {
            pad_abilities |= entry.source;
        }
    }

    if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)) {
        pad_abilities |= RV_ISOURCE_LEFT_STICK_MOVE | RV_ISOURCE_LEFT_STICK_DPAD_NORTH | RV_ISOURCE_LEFT_STICK_DPAD_SOUTH |
            RV_ISOURCE_LEFT_STICK_DPAD_WEST | RV_ISOURCE_LEFT_STICK_DPAD_EAST;
    }
    if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)) {
        pad_abilities |= RV_ISOURCE_RIGHT_STICK_MOVE | RV_ISOURCE_RIGHT_STICK_DPAD_NORTH | RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH |
            RV_ISOURCE_RIGHT_STICK_DPAD_WEST | RV_ISOURCE_RIGHT_STICK_DPAD_EAST;
    }
    const bool stick_touch = has_stick_touch_buttons(pad);
    const bool left_touch = stick_touch && SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_MISC3);
    const bool right_touch = stick_touch && SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_MISC4);
    if (left_touch) {
        pad_abilities |= RV_ISOURCE_LEFT_STICK_TOUCH;
    }
    if (right_touch) {
        pad_abilities |= RV_ISOURCE_RIGHT_STICK_TOUCH;
    }
    if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) {
        pad_abilities |= RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL | RV_ISOURCE_LEFT_TRIGGER_FULL_PULL;
    }
    if (SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        pad_abilities |= RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL | RV_ISOURCE_RIGHT_TRIGGER_FULL_PULL;
    }
    pad_abilities |= rv_pcgamepads_sdl3_trackpad_abilities(pad);

    const bool has_sensors = has_motion_sensors(pad);
    const unsigned shown =
        (has_sensors ? RV_PCPAD_PART_MOTION : 0u) | (SDL_GetNumGamepadTouchpads(pad) > 0 ? RV_PCPAD_PART_TOUCHPAD : 0u);
    const unsigned missing = rv_pcpad_parts_of_type(SDL_GetGamepadType(pad)) & ~shown;
    if (missing) {
        warn_missing_parts(pad, missing);
    }
    const bool motion = has_sensors && enable_motion_sensors(pad) == RV_OK;
    if (has_sensors && !motion) {
        motion_refused_ = true;
    }
    if (motion) {
        pad_abilities |= RV_ISOURCE_GYRO_MOVE | RV_ISOURCE_GYRO_PITCH | RV_ISOURCE_GYRO_YAW | RV_ISOURCE_GYRO_ROLL;
    }

    rv_pcpad_sdl3 entry;
    entry.pad = pad;
    entry.abilities = pad_abilities;
    entry.motion = motion;
    RV_LOG_INFO("pcplatform", "gamepad '{}' connected (id {})", SDL_GetGamepadName(pad), joystick_id);
    pads_.emplace(joystick_id, entry);
    connected_.push_back(joystick_id);
    ++generation_;
}

} // namespace rv_3dmppc
