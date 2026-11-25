// Axis value -> raw joystick value for the virtual touch pad. No SDL here, so
// the static_asserts below are checked by every compile that includes this.
#pragma once

#include <cstdint>

namespace rv_3dmppc
{

// These repeat SDL_GamepadAxis in SDL3 SDL_gamepad.h; keep them in step with it.
constexpr int RV_PCTOUCH_AXIS_LEFTX = 0;
constexpr int RV_PCTOUCH_AXIS_LEFTY = 1;
constexpr int RV_PCTOUCH_AXIS_RIGHTX = 2;
constexpr int RV_PCTOUCH_AXIS_RIGHTY = 3;
constexpr int RV_PCTOUCH_AXIS_LEFT_TRIGGER = 4;
constexpr int RV_PCTOUCH_AXIS_RIGHT_TRIGGER = 5;

// Round half away from zero; std::lround is not reliably constexpr.
constexpr long rv_pctouch_round(float v)
{
    return static_cast<long>(v < 0.0f ? v - 0.5f : v + 0.5f);
}

// Why triggers span the whole axis: SDL keeps a released virtual trigger at the axis minimum
// (SDL_virtualjoystick.c) and maps the whole axis range onto 0..1, so 0 must be -32768, not 0.
constexpr int16_t rv_pctouch_axis_raw(int axis, float value)
{
    if (axis == RV_PCTOUCH_AXIS_LEFT_TRIGGER || axis == RV_PCTOUCH_AXIS_RIGHT_TRIGGER) {
        const float t = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<int16_t>(rv_pctouch_round(t * 65535.0f) - 32768); // 0..65535 -> -32768..32767
    }
    const bool stick_y = axis == RV_PCTOUCH_AXIS_LEFTY || axis == RV_PCTOUCH_AXIS_RIGHTY;
    const float clamped = value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
    return static_cast<int16_t>(rv_pctouch_round((stick_y ? -clamped : clamped) * 32767.0f));
}

// Triggers: a released virtual trigger rests at the axis minimum, so 0 maps to -32768.
static_assert(rv_pctouch_axis_raw(RV_PCTOUCH_AXIS_LEFT_TRIGGER, 0.0f) == -32768, "trigger 0 is the axis minimum");
static_assert(rv_pctouch_axis_raw(RV_PCTOUCH_AXIS_RIGHT_TRIGGER, 1.0f) == 32767, "trigger 1 is the axis maximum");
static_assert(rv_pctouch_axis_raw(RV_PCTOUCH_AXIS_LEFT_TRIGGER, 0.5f) >= -1 &&
        rv_pctouch_axis_raw(RV_PCTOUCH_AXIS_LEFT_TRIGGER, 0.5f) <= 1,
    "trigger 0.5 is the axis middle");
static_assert(rv_pctouch_axis_raw(RV_PCTOUCH_AXIS_RIGHT_TRIGGER, -0.5f) == -32768, "trigger below 0 is cut at 0");
static_assert(rv_pctouch_axis_raw(RV_PCTOUCH_AXIS_LEFT_TRIGGER, 2.0f) == 32767, "trigger above 1 is cut at 1");

// Sticks keep the symmetric range, and Y is flipped (up is negative in SDL).
static_assert(rv_pctouch_axis_raw(RV_PCTOUCH_AXIS_LEFTX, 1.0f) == 32767, "stick X 1");
static_assert(rv_pctouch_axis_raw(RV_PCTOUCH_AXIS_LEFTX, -1.0f) == -32767, "stick X -1");
static_assert(rv_pctouch_axis_raw(RV_PCTOUCH_AXIS_LEFTY, 1.0f) == -32767, "stick Y 1 is up");

} // namespace rv_3dmppc
