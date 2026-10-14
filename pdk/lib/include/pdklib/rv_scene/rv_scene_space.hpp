#pragma once

// An object's transform from its Q20.12 fields. The console builds it here (through rv_scene_local).
// Inline because a disc gets pdklib only as headers and can take the same matrix.

#include <cmath>

#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_math/rv_math.hpp"

namespace rv_pdklib
{

constexpr float rv_scene_degrees = 3.14159265358979f / 180.0f;
constexpr float rv_scene_right_angle_degrees = 90.0f; // right angle for rotation checks
constexpr int rv_scene_rotation_quarters = 4;         // four 90-degree rotations make a full circle

// sin/cos of a degree angle, exact 0/1/-1 on a multiple of 90 (any sign, any
// winding) instead of the ~1e-8 float noise std::cos leaves there -- the
// difference an axis-aligned quad's corners (rv_scene_quad_corners) show up.
inline void rv_scene_sincos(float degrees, float &s, float &c)
{
    const float quarter = std::round(degrees / rv_scene_right_angle_degrees);
    if (quarter * rv_scene_right_angle_degrees == degrees) {
        constexpr float sins[rv_scene_rotation_quarters] = { 0.0f, 1.0f, 0.0f, -1.0f };
        constexpr float coss[rv_scene_rotation_quarters] = { 1.0f, 0.0f, -1.0f, 0.0f };
        int k = static_cast<int>(quarter) % rv_scene_rotation_quarters;
        if (k < 0) {
            k += rv_scene_rotation_quarters;
        }
        s = sins[k];
        c = coss[k];
        return;
    }
    const float radians = degrees * rv_scene_degrees;
    s = std::sin(radians);
    c = std::cos(radians);
}

// Same layout as rv_mat4_rotate_x/y/z (rv_math.hpp), built from rv_scene_sincos.
inline rv_mat4 rv_scene_rotate_x(float degrees)
{
    float s, c;
    rv_scene_sincos(degrees, s, c);
    rv_mat4 r = rv_mat4_identity();
    r.m[1][1] = c;
    r.m[1][2] = -s;
    r.m[2][1] = s;
    r.m[2][2] = c;
    return r;
}

inline rv_mat4 rv_scene_rotate_y(float degrees)
{
    float s, c;
    rv_scene_sincos(degrees, s, c);
    rv_mat4 r = rv_mat4_identity();
    r.m[0][0] = c;
    r.m[0][2] = s;
    r.m[2][0] = -s;
    r.m[2][2] = c;
    return r;
}

inline rv_mat4 rv_scene_rotate_z(float degrees)
{
    float s, c;
    rv_scene_sincos(degrees, s, c);
    rv_mat4 r = rv_mat4_identity();
    r.m[0][0] = c;
    r.m[0][1] = -s;
    r.m[1][0] = s;
    r.m[1][1] = c;
    return r;
}

// Local transform T * Ry * Rx * Rz * S; rotation is Q20.12 degrees as [x, y, z].
inline rv_mat4 rv_scene_local_trs(fx_vec3 position, fx_vec3 rotation, fx_vec3 scale)
{
    const rv_vec3 p = fx_vec3_to_float(position);
    const rv_vec3 rot = fx_vec3_to_float(rotation);
    const rv_vec3 sc = fx_vec3_to_float(scale);
    const rv_mat4 r = rv_mat4_mul(rv_scene_rotate_y(rot.y), rv_mat4_mul(rv_scene_rotate_x(rot.x), rv_scene_rotate_z(rot.z)));
    return rv_mat4_mul(rv_mat4_translate(p), rv_mat4_mul(r, rv_mat4_scale(sc)));
}

} // namespace rv_pdklib
