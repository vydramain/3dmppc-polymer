#pragma once

// Face-camera system of the scene ECS: turns each entity with ecs_component_face_camera to the frame's camera.
// Runs after ecs_system_transform. Only the rotation of the world matrix changes; the scale and the translation stay.

#include <cstdint>
#include <vector>

#include "pdk/cg/rv_cg.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"

namespace rv_pdkkit
{

// Reason texts of the refused cameras and turns; the frame logs them with the code.
inline constexpr const char *ecs_face_camera_zero_row = "camera view has a zero row";
inline constexpr const char *ecs_face_camera_turned_out_of_range = "the turned matrix leaves its range";

// Unit axes of the camera in 4.12: right, up, and back (the direction from target to eye, the camera's -z).
struct ecs_camera_axes {
    std::int16_t right[3]{};
    std::int16_t up[3]{};
    std::int16_t back[3]{};
};

// Integer length of a vector of raw values, in the scale of its inputs: the root of the squares, floored.
inline std::int64_t ecs_face_camera_length(std::int64_t x, std::int64_t y, std::int64_t z)
{
    const auto sum = static_cast<std::uint64_t>(x * x + y * y + z * z);
    return static_cast<std::int64_t>(rv_pdklib::fx_isqrt(sum));
}

// View rows are right, up and forward (rv_gte_camera_rows); right is -row 0 and back is -row 2. The turned columns
// (right, up, back) must form a rotation, det +1: with right = +row 0 they form a reflection, and back-face culling
// would drop the card's front. A zero row has no direction: RV_ERR_INVAL with its reason; out is then untouched.
[[nodiscard]] inline int ecs_camera_axes_of(const rv_cg_camera &camera, ecs_camera_axes &out, ecs_reason &reason)
{
    for (int row = 0; row < 3; ++row) {
        const std::int16_t *r = camera.view.r + row * 3;
        const bool zero = r[0] == 0 && r[1] == 0 && r[2] == 0;
        if (zero) {
            ecs_reason_refuse(reason, ecs_face_camera_zero_row);
            return RV_ERR_INVAL;
        }
    }
    for (int i = 0; i < 3; ++i) {
        out.right[i] = static_cast<std::int16_t>(-camera.view.r[0 + i]);
        out.up[i] = camera.view.r[3 + i];
        out.back[i] = static_cast<std::int16_t>(-camera.view.r[6 + i]);
    }
    return RV_OK;
}

// Columns 0..2 become the camera's right, up and back, each scaled to the length of the column it replaces, rounded
// to 4.12. The translation in column 3 is not touched. RV_ERR_INVAL for a value outside int16; world is then untouched.
[[nodiscard]] inline int ecs_face_camera_apply(rv_pdklib::rv_gte_matrix &world, const ecs_camera_axes &axes, ecs_reason &reason)
{
    const std::int16_t *axis_of[3] = { axes.right, axes.up, axes.back };
    rv_pdklib::rv_gte_matrix turned = world;
    for (int col = 0; col < 3; ++col) {
        const std::int64_t own_len = ecs_face_camera_length(world.r[0 + col], world.r[3 + col], world.r[6 + col]);
        for (int row = 0; row < 3; ++row) {
            const std::int64_t scaled = rv_pdklib::rv_gte_round_frac(static_cast<std::int64_t>(axis_of[col][row]) * own_len);
            if (!rv_pdklib::rv_gte_fits<std::int16_t>(scaled)) {
                ecs_reason_refuse(reason, ecs_face_camera_turned_out_of_range);
                return RV_ERR_INVAL;
            }
            turned.r[row * 3 + col] = static_cast<std::int16_t>(scaled);
        }
    }
    world = turned;
    return RV_OK;
}

// Writes the rotation of every face-camera entity's ecs_component_world. Without a camera set this does nothing and
// returns RV_OK; the draw then reports the missing camera. RV_ERR_INVAL for a zero view row or a turned value that
// leaves int16, with its reason in reason; ecs_frame_run reports it. An entity with no world matrix is skipped: the
// transform left it out, so there is nothing to turn; a visible model without one still fails in ecs_draw_entity.
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
            const int turned = ecs_face_camera_apply(place->matrix, axes, reason);
            if (turned != RV_OK) {
                return turned;
            }
        }
        return RV_OK;
    });
}

} // namespace rv_pdkkit
