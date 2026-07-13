// First-person camera: position + yaw/pitch → left-handed view matrix, plus the
// projection for the console's native framebuffer.
//
// SHARED CONTRACT — owned by the orchestrator. The player system (Agent B) fills
// it from the player transform (+ head-bob / shake offsets); the renderer
// (Agent E) reads view()/proj() to transform the scene.
#pragma once

#include "math/math.hpp"

namespace rv_3dmppc {

struct Camera {
    Vec3 pos{0, 0, 0};
    float yaw = 0.0f;    // radians, around +y (0 = looking +z)
    float pitch = 0.0f;  // radians, + looks up (clamp in the player system)
    float fovYRadians = radians(70.0f);
    float aspect = 320.0f / 240.0f;
    float zNear = 0.05f;
    float zFar = 100.0f;

    // Forward/right unit vectors implied by yaw/pitch (left-handed).
    Vec3 forward() const {
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        return {sy * cp, sp, cy * cp};
    }
    Vec3 right() const {
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        return {cy, 0.0f, -sy};
    }

    Mat4 view() const {
        const Vec3 fwd = forward();
        return lookAtLH(pos, pos + fwd, {0, 1, 0});
    }

    Mat4 proj() const {
        return perspectiveLH(fovYRadians, aspect, zNear, zFar);
    }

    Mat4 viewProj() const { return proj() * view(); }
};

}  // namespace rv_3dmppc
