#pragma once

#include <cstdint>

namespace rv_3dmppc {

// Every input source a controller port can carry, one bit each. The SAME
// vocabulary is used two ways:
//   * iport_abilities(port) — a STATIC capability mask ("this port has a gyro");
//   * rv_istate::buttons    — a LIVE mask of what is active/pressed right now.
// For an analog source a live bit means "out of dead zone / being touched"; the
// analog value itself lives in the rv_istate fields below.
//
// OPEN: the derived *_DPAD_* / *_MOVE bits interpret an analog source (a stick
// pushed past a threshold, in a direction) — arguably binding logic layered on
// top of the raw stick value. Kept for full Steam Deck coverage; revisit if the
// device surface should stay strictly raw. See README "Open decisions".
enum rv_isource : uint64_t {
    // Front buttons
    RV_ISOURCE_FRONT_BTTN_SOUTH = 1ULL << 0,
    RV_ISOURCE_FRONT_BTTN_EAST = 1ULL << 1,
    RV_ISOURCE_FRONT_BTTN_WEST = 1ULL << 2,
    RV_ISOURCE_FRONT_BTTN_NORTH = 1ULL << 3,

    // Bumpers
    RV_ISOURCE_BUMPER_LEFT = 1ULL << 4,
    RV_ISOURCE_BUMPER_RIGHT = 1ULL << 5,

    // Menu buttons
    RV_ISOURCE_MENU_BTTN_MENU = 1ULL << 6,
    RV_ISOURCE_MENU_BTTN_VIEW = 1ULL << 7,

    // Left trackpad
    RV_ISOURCE_LEFT_TRACKPAD_TOUCH = 1ULL << 8,
    RV_ISOURCE_LEFT_TRACKPAD_SWIPE = 1ULL << 9,
    RV_ISOURCE_LEFT_TRACKPAD_CLICK = 1ULL << 10,
    RV_ISOURCE_LEFT_TRACKPAD_DPAD_NORTH = 1ULL << 11,
    RV_ISOURCE_LEFT_TRACKPAD_DPAD_SOUTH = 1ULL << 12,
    RV_ISOURCE_LEFT_TRACKPAD_DPAD_WEST = 1ULL << 13,
    RV_ISOURCE_LEFT_TRACKPAD_DPAD_EAST = 1ULL << 14,

    // Right trackpad
    RV_ISOURCE_RIGHT_TRACKPAD_TOUCH = 1ULL << 15,
    RV_ISOURCE_RIGHT_TRACKPAD_SWIPE = 1ULL << 16,
    RV_ISOURCE_RIGHT_TRACKPAD_CLICK = 1ULL << 17,
    RV_ISOURCE_RIGHT_TRACKPAD_DPAD_NORTH = 1ULL << 18,
    RV_ISOURCE_RIGHT_TRACKPAD_DPAD_SOUTH = 1ULL << 19,
    RV_ISOURCE_RIGHT_TRACKPAD_DPAD_WEST = 1ULL << 20,
    RV_ISOURCE_RIGHT_TRACKPAD_DPAD_EAST = 1ULL << 21,

    // Left trigger
    RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL = 1ULL << 22,
    RV_ISOURCE_LEFT_TRIGGER_FULL_PULL = 1ULL << 23,

    // Right trigger
    RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL = 1ULL << 24,
    RV_ISOURCE_RIGHT_TRIGGER_FULL_PULL = 1ULL << 25,

    // Left stick
    RV_ISOURCE_LEFT_STICK_MOVE = 1ULL << 26,
    RV_ISOURCE_LEFT_STICK_CLICK = 1ULL << 27,
    RV_ISOURCE_LEFT_STICK_DPAD_NORTH = 1ULL << 28,
    RV_ISOURCE_LEFT_STICK_DPAD_SOUTH = 1ULL << 29,
    RV_ISOURCE_LEFT_STICK_DPAD_WEST = 1ULL << 30,
    RV_ISOURCE_LEFT_STICK_DPAD_EAST = 1ULL << 31,
    RV_ISOURCE_LEFT_STICK_TOUCH = 1ULL << 32,

    // Right stick
    RV_ISOURCE_RIGHT_STICK_MOVE = 1ULL << 33,
    RV_ISOURCE_RIGHT_STICK_CLICK = 1ULL << 34,
    RV_ISOURCE_RIGHT_STICK_DPAD_NORTH = 1ULL << 35,
    RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH = 1ULL << 36,
    RV_ISOURCE_RIGHT_STICK_DPAD_WEST = 1ULL << 37,
    RV_ISOURCE_RIGHT_STICK_DPAD_EAST = 1ULL << 38,
    RV_ISOURCE_RIGHT_STICK_TOUCH = 1ULL << 39,

    // Rear buttons
    RV_ISOURCE_REAR_BTTN_LEFT_UPPER = 1ULL << 40,
    RV_ISOURCE_REAR_BTTN_RIGHT_UPPER = 1ULL << 41,
    RV_ISOURCE_REAR_BTTN_LEFT_LOWER = 1ULL << 42,
    RV_ISOURCE_REAR_BTTN_RIGHT_LOWER = 1ULL << 43,

    // Directional pad
    RV_ISOURCE_DPAD_MOVE = 1ULL << 44,
    RV_ISOURCE_DPAD_NORTH = 1ULL << 45,
    RV_ISOURCE_DPAD_SOUTH = 1ULL << 46,
    RV_ISOURCE_DPAD_WEST = 1ULL << 47,
    RV_ISOURCE_DPAD_EAST = 1ULL << 48,

    // Gyroscope
    RV_ISOURCE_GYRO_MOVE = 1ULL << 49,
    RV_ISOURCE_GYRO_PITCH = 1ULL << 50,
    RV_ISOURCE_GYRO_YAW = 1ULL << 51,
    RV_ISOURCE_GYRO_ROLL = 1ULL << 52,
};

// A 2D analog value (stick or trackpad), each axis normalized to [-1, 1].
struct rv_iaxes {
    float x, y;
};

// Inertial (gyro/accelerometer) sample for a motion-capable controller.
struct rv_imotion {
    // Orientation (unit quaternion)
    float quat_x;
    float quat_y;
    float quat_z;
    float quat_w;

    // Linear acceleration
    float accel_x;
    float accel_y;
    float accel_z;

    // Angular velocity
    float angular_velocity_x;
    float angular_velocity_y;
    float angular_velocity_z;
};

// Instantaneous state of one controller port, filled by rv_cio::iport_state().
struct rv_istate {
    // Live rv_isource bitmask: held digital buttons, plus liveness bits for
    // analog sources (set when out of dead zone / touched).
    uint64_t buttons;

    // Sticks
    rv_iaxes left_stick;
    rv_iaxes right_stick;

    // Trackpads
    rv_iaxes left_trackpad;
    rv_iaxes right_trackpad;

    // Triggers
    float left_trigger;
    float right_trigger;

    rv_imotion motion;
};

}  // namespace rv_3dmppc
