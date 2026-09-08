#ifndef RV_PDK_CIO_RV_ISOURCE_H
#define RV_PDK_CIO_RV_ISOURCE_H

#include <stdint.h>

// Front buttons
#define RV_ISOURCE_FRONT_BTTN_SOUTH UINT64_C(0x0000000000000001) // bit 0
#define RV_ISOURCE_FRONT_BTTN_EAST  UINT64_C(0x0000000000000002) // bit 1
#define RV_ISOURCE_FRONT_BTTN_WEST  UINT64_C(0x0000000000000004) // bit 2
#define RV_ISOURCE_FRONT_BTTN_NORTH UINT64_C(0x0000000000000008) // bit 3

// Bumpers
#define RV_ISOURCE_BUMPER_LEFT  UINT64_C(0x0000000000000010) // bit 4
#define RV_ISOURCE_BUMPER_RIGHT UINT64_C(0x0000000000000020) // bit 5

// Menu buttons
#define RV_ISOURCE_MENU_BTTN_MENU UINT64_C(0x0000000000000040) // bit 6
#define RV_ISOURCE_MENU_BTTN_VIEW UINT64_C(0x0000000000000080) // bit 7

// Left trackpad
#define RV_ISOURCE_LEFT_TRACKPAD_TOUCH      UINT64_C(0x0000000000000100) // bit 8
#define RV_ISOURCE_LEFT_TRACKPAD_SWIPE      UINT64_C(0x0000000000000200) // bit 9
#define RV_ISOURCE_LEFT_TRACKPAD_CLICK      UINT64_C(0x0000000000000400) // bit 10
#define RV_ISOURCE_LEFT_TRACKPAD_DPAD_NORTH UINT64_C(0x0000000000000800) // bit 11
#define RV_ISOURCE_LEFT_TRACKPAD_DPAD_SOUTH UINT64_C(0x0000000000001000) // bit 12
#define RV_ISOURCE_LEFT_TRACKPAD_DPAD_WEST  UINT64_C(0x0000000000002000) // bit 13
#define RV_ISOURCE_LEFT_TRACKPAD_DPAD_EAST  UINT64_C(0x0000000000004000) // bit 14

// Right trackpad
#define RV_ISOURCE_RIGHT_TRACKPAD_TOUCH      UINT64_C(0x0000000000008000) // bit 15
#define RV_ISOURCE_RIGHT_TRACKPAD_SWIPE      UINT64_C(0x0000000000010000) // bit 16
#define RV_ISOURCE_RIGHT_TRACKPAD_CLICK      UINT64_C(0x0000000000020000) // bit 17
#define RV_ISOURCE_RIGHT_TRACKPAD_DPAD_NORTH UINT64_C(0x0000000000040000) // bit 18
#define RV_ISOURCE_RIGHT_TRACKPAD_DPAD_SOUTH UINT64_C(0x0000000000080000) // bit 19
#define RV_ISOURCE_RIGHT_TRACKPAD_DPAD_WEST  UINT64_C(0x0000000000100000) // bit 20
#define RV_ISOURCE_RIGHT_TRACKPAD_DPAD_EAST  UINT64_C(0x0000000000200000) // bit 21

// Left trigger
#define RV_ISOURCE_LEFT_TRIGGER_SOFT_PULL UINT64_C(0x0000000000400000) // bit 22
#define RV_ISOURCE_LEFT_TRIGGER_FULL_PULL UINT64_C(0x0000000000800000) // bit 23

// Right trigger
#define RV_ISOURCE_RIGHT_TRIGGER_SOFT_PULL UINT64_C(0x0000000001000000) // bit 24
#define RV_ISOURCE_RIGHT_TRIGGER_FULL_PULL UINT64_C(0x0000000002000000) // bit 25

// Left stick
#define RV_ISOURCE_LEFT_STICK_MOVE       UINT64_C(0x0000000004000000) // bit 26
#define RV_ISOURCE_LEFT_STICK_CLICK      UINT64_C(0x0000000008000000) // bit 27
#define RV_ISOURCE_LEFT_STICK_DPAD_NORTH UINT64_C(0x0000000010000000) // bit 28
#define RV_ISOURCE_LEFT_STICK_DPAD_SOUTH UINT64_C(0x0000000020000000) // bit 29
#define RV_ISOURCE_LEFT_STICK_DPAD_WEST  UINT64_C(0x0000000040000000) // bit 30
#define RV_ISOURCE_LEFT_STICK_DPAD_EAST  UINT64_C(0x0000000080000000) // bit 31
#define RV_ISOURCE_LEFT_STICK_TOUCH      UINT64_C(0x0000000100000000) // bit 32

// Right stick
#define RV_ISOURCE_RIGHT_STICK_MOVE       UINT64_C(0x0000000200000000) // bit 33
#define RV_ISOURCE_RIGHT_STICK_CLICK      UINT64_C(0x0000000400000000) // bit 34
#define RV_ISOURCE_RIGHT_STICK_DPAD_NORTH UINT64_C(0x0000000800000000) // bit 35
#define RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH UINT64_C(0x0000001000000000) // bit 36
#define RV_ISOURCE_RIGHT_STICK_DPAD_WEST  UINT64_C(0x0000002000000000) // bit 37
#define RV_ISOURCE_RIGHT_STICK_DPAD_EAST  UINT64_C(0x0000004000000000) // bit 38
#define RV_ISOURCE_RIGHT_STICK_TOUCH      UINT64_C(0x0000008000000000) // bit 39

// Rear buttons
#define RV_ISOURCE_REAR_BTTN_LEFT_UPPER  UINT64_C(0x0000010000000000) // bit 40
#define RV_ISOURCE_REAR_BTTN_RIGHT_UPPER UINT64_C(0x0000020000000000) // bit 41
#define RV_ISOURCE_REAR_BTTN_LEFT_LOWER  UINT64_C(0x0000040000000000) // bit 42
#define RV_ISOURCE_REAR_BTTN_RIGHT_LOWER UINT64_C(0x0000080000000000) // bit 43

// Directional pad
#define RV_ISOURCE_DPAD_MOVE  UINT64_C(0x0000100000000000) // bit 44
#define RV_ISOURCE_DPAD_NORTH UINT64_C(0x0000200000000000) // bit 45
#define RV_ISOURCE_DPAD_SOUTH UINT64_C(0x0000400000000000) // bit 46
#define RV_ISOURCE_DPAD_WEST  UINT64_C(0x0000800000000000) // bit 47
#define RV_ISOURCE_DPAD_EAST  UINT64_C(0x0001000000000000) // bit 48

// Gyroscope
#define RV_ISOURCE_GYRO_MOVE  UINT64_C(0x0002000000000000) // bit 49
#define RV_ISOURCE_GYRO_PITCH UINT64_C(0x0004000000000000) // bit 50
#define RV_ISOURCE_GYRO_YAW   UINT64_C(0x0008000000000000) // bit 51
#define RV_ISOURCE_GYRO_ROLL  UINT64_C(0x0010000000000000) // bit 52

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef uint64_t rv_isource;

struct rv_iaxes {
    float x, y;
};

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

struct rv_istate {
    // Live rv_isource bitmask: held digital buttons, plus liveness bits for
    // analog sources (set when out of dead zone / touched).
    uint64_t buttons;

    // Sticks
    struct rv_iaxes left_stick;
    struct rv_iaxes right_stick;

    // Trackpads
    struct rv_iaxes left_trackpad;
    struct rv_iaxes right_trackpad;

    // Triggers, normalized to [0, 1] — 0 released, 1 fully pulled.
    float left_trigger;
    float right_trigger;

    struct rv_imotion motion;
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef struct rv_iaxes rv_iaxes;
typedef struct rv_imotion rv_imotion;
typedef struct rv_istate rv_istate;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CIO_RV_ISOURCE_H
