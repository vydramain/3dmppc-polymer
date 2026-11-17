// Which parts a gamepad type has that a browser may fail to pass (gyro, touchpad). No SDL here,
// so the static_asserts below are checked by every compile that includes this.
#pragma once

#include <string_view>

namespace rv_3dmppc
{

// These repeat SDL_GamepadType in SDL3 SDL_gamepad.h; keep them in step with it.
constexpr int RV_PCPAD_TYPE_UNKNOWN = 0;
constexpr int RV_PCPAD_TYPE_STANDARD = 1;
constexpr int RV_PCPAD_TYPE_XBOX360 = 2;
constexpr int RV_PCPAD_TYPE_XBOXONE = 3;
constexpr int RV_PCPAD_TYPE_PS3 = 4;
constexpr int RV_PCPAD_TYPE_PS4 = 5;
constexpr int RV_PCPAD_TYPE_PS5 = 6;
constexpr int RV_PCPAD_TYPE_NINTENDO_SWITCH_PRO = 7;
constexpr int RV_PCPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT = 8;
constexpr int RV_PCPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT = 9;
constexpr int RV_PCPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR = 10;
constexpr int RV_PCPAD_TYPE_GAMECUBE = 11;
constexpr int RV_PCPAD_TYPE_STEAM = 12;

constexpr unsigned RV_PCPAD_PART_MOTION = 1;
constexpr unsigned RV_PCPAD_PART_TOUCHPAD = 2;

// PS4 and PS5 pads have a gyro and a touchpad; Nintendo pads have a gyro; the rest have neither.
constexpr unsigned rv_pcpad_parts_of_type(int type)
{
    switch (type) {
    case RV_PCPAD_TYPE_PS4:
    case RV_PCPAD_TYPE_PS5:
        return RV_PCPAD_PART_MOTION | RV_PCPAD_PART_TOUCHPAD;
    case RV_PCPAD_TYPE_NINTENDO_SWITCH_PRO:
    case RV_PCPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case RV_PCPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case RV_PCPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return RV_PCPAD_PART_MOTION;
    default:
        return 0;
    }
}

// The parts as words for a log line.
constexpr const char *rv_pcpad_parts_text(unsigned parts)
{
    if (parts == (RV_PCPAD_PART_MOTION | RV_PCPAD_PART_TOUCHPAD)) {
        return "motion sensors and touchpad";
    }
    if (parts == RV_PCPAD_PART_MOTION) {
        return "motion sensors";
    }
    return parts == RV_PCPAD_PART_TOUCHPAD ? "touchpad" : "";
}

constexpr bool rv_pcpad_text_is(unsigned parts, std::string_view want)
{
    return std::string_view(rv_pcpad_parts_text(parts)) == want;
}

static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_PS4) == (RV_PCPAD_PART_MOTION | RV_PCPAD_PART_TOUCHPAD), "PS4");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_PS5) == (RV_PCPAD_PART_MOTION | RV_PCPAD_PART_TOUCHPAD), "PS5");

static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_NINTENDO_SWITCH_PRO) == RV_PCPAD_PART_MOTION, "Switch Pro");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT) == RV_PCPAD_PART_MOTION, "Joy-Con L");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT) == RV_PCPAD_PART_MOTION, "Joy-Con R");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR) == RV_PCPAD_PART_MOTION, "Joy-Con pair");

static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_UNKNOWN) == 0, "unknown has nothing");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_STANDARD) == 0, "standard has nothing");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_XBOX360) == 0, "Xbox 360 has nothing");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_XBOXONE) == 0, "Xbox One has nothing");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_PS3) == 0, "PS3 has nothing");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_GAMECUBE) == 0, "GameCube has nothing");
static_assert(rv_pcpad_parts_of_type(RV_PCPAD_TYPE_STEAM) == 0, "Steam has nothing");

static_assert(rv_pcpad_text_is(RV_PCPAD_PART_MOTION | RV_PCPAD_PART_TOUCHPAD, "motion sensors and touchpad"), "text both");
static_assert(rv_pcpad_text_is(RV_PCPAD_PART_MOTION, "motion sensors"), "text motion");
static_assert(rv_pcpad_text_is(RV_PCPAD_PART_TOUCHPAD, "touchpad"), "text touchpad");
static_assert(rv_pcpad_text_is(0, ""), "text none");

} // namespace rv_3dmppc
