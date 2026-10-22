#pragma once

// Face-camera system of the scene ECS: turns each entity with ecs_component_face_camera to the frame's camera.
// Runs after ecs_system_transform. Only the rotation of the world matrix changes; the scale and the translation stay.

#include <cstring>
#include <vector>

#include "pdk/cg/rv_cg.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_math/rv_math.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"

namespace rv_pdkkit
{

// A camera vector shorter than this (world units) is zero: eye on target, or up with no length.
inline constexpr float ecs_face_camera_epsilon = 1e-6f;

// Sine of up against the view, of unit vectors. Looser than the length test: the cross product of unit floats carries
// an error around 1e-7, and a sine that close to the threshold would pick a wrong right vector.
inline constexpr float ecs_face_camera_sine_epsilon = 1e-4f;

// Reason texts of the refused cameras; the frame logs them with the code.
inline constexpr const char *ecs_face_camera_no_view = "camera eye is on its target";
inline constexpr const char *ecs_face_camera_no_up = "camera up is zero";
inline constexpr const char *ecs_face_camera_up_along_view = "camera up is along the view";

// Unit axes of the camera: right, the true up, and back (the direction from target to eye, the camera's -z).
struct ecs_camera_axes {
    rv_pdklib::rv_vec3 right{};
    rv_pdklib::rv_vec3 up{};
    rv_pdklib::rv_vec3 back{};
};

// Writes the reason text of a returned refusal. thrown stays clear: the frame logs it as returned, with the text.
inline void ecs_face_camera_refuse(ecs_reason &reason, const char *text)
{
    std::strncpy(reason.text, text, ecs_reason_size - 1);
    reason.text[ecs_reason_size - 1] = '\0';
}

// RV_ERR_INVAL for a degenerate camera, with its case in reason; out is written only on RV_OK.
[[nodiscard]] inline int ecs_camera_axes_of(const rv_cg_camera &camera, ecs_camera_axes &out, ecs_reason &reason)
{
    const rv_pdklib::rv_vec3 eye{ camera.eye.x, camera.eye.y, camera.eye.z };
    const rv_pdklib::rv_vec3 target{ camera.target.x, camera.target.y, camera.target.z };
    const rv_pdklib::rv_vec3 up_in{ camera.up.x, camera.up.y, camera.up.z };
    const rv_pdklib::rv_vec3 view = eye - target;
    if (rv_pdklib::rv_length(view) <= ecs_face_camera_epsilon) {
        ecs_face_camera_refuse(reason, ecs_face_camera_no_view);
        return RV_ERR_INVAL;
    }
    if (rv_pdklib::rv_length(up_in) <= ecs_face_camera_epsilon) {
        ecs_face_camera_refuse(reason, ecs_face_camera_no_up);
        return RV_ERR_INVAL;
    }
    const rv_pdklib::rv_vec3 back = rv_pdklib::rv_normalize(view);
    const rv_pdklib::rv_vec3 up_unit = rv_pdklib::rv_normalize(up_in);
    const rv_pdklib::rv_vec3 side = rv_pdklib::rv_cross(up_unit, back);
    if (rv_pdklib::rv_length(side) <= ecs_face_camera_sine_epsilon) {
        ecs_face_camera_refuse(reason, ecs_face_camera_up_along_view);
        return RV_ERR_INVAL;
    }
    out.back = back;
    out.right = rv_pdklib::rv_normalize(side);
    out.up = rv_pdklib::rv_cross(back, out.right);
    return RV_OK;
}

// Columns 0..2 become the camera's right, up and back, each scaled to the length of the column it replaces.
// The translation in column 3 is not touched.
inline void ecs_face_camera_apply(rv_pdklib::rv_mat4 &world, const ecs_camera_axes &axes)
{
    const rv_pdklib::rv_vec3 axis_of[3] = { axes.right, axes.up, axes.back };
    for (int col = 0; col < 3; ++col) {
        const rv_pdklib::rv_vec3 own{ world.m[0][col], world.m[1][col], world.m[2][col] };
        const rv_pdklib::rv_vec3 turned = axis_of[col] * rv_pdklib::rv_length(own);
        world.m[0][col] = turned.x;
        world.m[1][col] = turned.y;
        world.m[2][col] = turned.z;
    }
}

// Writes the rotation of every face-camera entity's ecs_component_world. Without a camera set this does nothing and
// returns RV_OK; the draw then reports the missing camera. RV_ERR_INVAL for a degenerate camera, with its case in
// reason; ecs_frame_run reports it. An entity with no world matrix is skipped: the transform left it out, so there is
// nothing to turn; a visible model without one still fails in ecs_draw_entity.
[[nodiscard]] inline int ecs_system_face_camera(ecs_world &world, ecs_reason &reason)
{
    const auto &state = detail::ecs_state(world);
    if (!state.camera_set) {
        return RV_OK;
    }
    return ecs_guard([&]() -> int {
        ecs_camera_axes axes{};
        const int basis = ecs_camera_axes_of(state.camera, axes, reason);
        if (basis != RV_OK) {
            return basis;
        }
        // Entities are listed first, as in ecs_system_transform: the loop below writes components.
        std::vector<ecs_id> holders;
        ecs_component_each<ecs_component_face_camera>(world, [&holders](ecs_id id, ecs_component_face_camera &) {
            holders.push_back(id);
        });
        for (const ecs_id id : holders) {
            ecs_component_world *place = ecs_component_get<ecs_component_world>(world, id);
            if (place == nullptr) {
                continue;
            }
            ecs_face_camera_apply(place->matrix, axes);
        }
        return RV_OK;
    });
}

} // namespace rv_pdkkit
