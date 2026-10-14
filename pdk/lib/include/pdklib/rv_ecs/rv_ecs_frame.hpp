#pragma once

// Frame of the scene ECS: the frame record, the registry of frame systems, the camera, the draw system and the frame order.
// Draw and systems return codes without logging: a frame repeats them, so the disc logs the first one.
// ecs_system_transform logs a broken parent chain with its reason; the scene loader never builds one.

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/cv/rv_primitives.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_ecs/rv_ecs.hpp"
#include "pdklib/rv_ecs/rv_ecs_component.hpp"
#include "pdklib/rv_ecs/rv_ecs_store.hpp"
#include "pdklib/rv_ecs/rv_ecs_transform.hpp"

namespace rv_pdklib
{

// Handle of one frame: the disc's drive, and the frame number (the disc's logic time).
struct ecs_frame {
    rv_pdko *pdk = nullptr;
    int64_t frame = 0;
};

// Ordering-table windows of the world and of decals, inside RV_DEPTH_MIN..RV_DEPTH_MAX (pdk/cv/rv_primitives.h).
// The bias of 260 keys is the project's decal bias, the same as Solid Maid's SM_DEPTH_BIAS_DECAL.
// The decal window is the world window shifted the bias nearer, same width, so a decal wins over the wall it lies on.
inline constexpr int32_t ecs_depth_decal_bias = 260;
inline constexpr int32_t ecs_depth_world_min = RV_DEPTH_MIN;
inline constexpr int32_t ecs_depth_world_max = RV_DEPTH_MAX - ecs_depth_decal_bias;
inline constexpr int32_t ecs_depth_decal_min = RV_DEPTH_MIN + ecs_depth_decal_bias;
inline constexpr int32_t ecs_depth_decal_max = RV_DEPTH_MAX;

// RV_ERR_INVAL for an empty name or a null system, RV_ERR_ALREADY when the name is taken.
inline int ecs_system_register(ecs_world &world, std::string_view name, ecs_system run)
{
    const bool bad = name.empty() || run == nullptr;
    if (bad) {
        return RV_ERR_INVAL;
    }
    for (const auto &entry : world.systems) {
        if (entry.first == name) {
            return RV_ERR_ALREADY;
        }
    }
    world.systems.emplace_back(name, run);
    return RV_OK;
}

// Camera of the frame, for the puts that follow it. A system sets it; ecs_system_draw needs it.
inline void ecs_world_camera(ecs_world &world, const rv_cg_camera &camera)
{
    world.camera = camera;
    world.camera_set = true;
}

// Puts one entity's model. Dead, model-less and hidden entities are skipped; a decal takes the decal window.
inline int ecs_draw_entity(ecs_world &world, const ecs_frame &frame, ecs_id id)
{
    const ecs_component_model *model = ecs_component_get<ecs_component_model>(world, id);
    const bool absent = !ecs_entity_alive(world, id) || model == nullptr;
    const bool hidden = ecs_component_get<ecs_component_hidden>(world, id) != nullptr;
    if (absent || hidden) {
        return RV_OK;
    }
    const ecs_component_world *place = ecs_component_get<ecs_component_world>(world, id);
    if (place == nullptr) {
        return RV_ERR_INVAL;
    }
    const bool decal = ecs_component_get<ecs_component_decal>(world, id) != nullptr;
    const int32_t depth_min = decal ? ecs_depth_decal_min : ecs_depth_world_min;
    const int32_t depth_max = decal ? ecs_depth_decal_max : ecs_depth_world_max;
    const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(frame.pdk), RV_CD_RESOURCE_MODEL, model->name.c_str());
    if (addr < 0) {
        return static_cast<int>(addr);
    }
    rv_cg_put put = {};
    static_assert(sizeof(rv_mat4::m) == sizeof(rv_cg_put::world), "rv_mat4 and rv_cg_put.world hold the same 16 floats");
    std::memcpy(put.world, place->matrix.m, sizeof(put.world));
    put.depth_min = depth_min;
    put.depth_max = depth_max;
    put.cull = RV_CG_CULL_BACK;
    put.tint = model->tint;
    const int64_t count = rv_cg_model_put(rv_pdko_cg(frame.pdk), addr, &put);
    if (count < 0) {
        return static_cast<int>(count);
    }
    return RV_OK;
}

// Puts the models in the order given, inside the frame's camera. RV_ERR_NOENT without a camera: nothing is put.
// A failed entity does not stop the rest, as the disc draws on; the first failure's code is returned.
inline int ecs_system_draw(ecs_world &world, const ecs_frame &frame, const std::vector<ecs_id> &order)
{
    if (!world.camera_set) {
        return RV_ERR_NOENT;
    }
    const int64_t camera_code = rv_cg_frame_camera(rv_pdko_cg(frame.pdk), &world.camera);
    if (camera_code < 0) {
        return static_cast<int>(camera_code);
    }
    int first_error = RV_OK;
    for (const ecs_id id : order) {
        const int code = ecs_draw_entity(world, frame, id);
        const bool first_failure = first_error == RV_OK && code != RV_OK;
        first_error = first_failure ? code : first_error;
    }
    return first_error;
}

// One frame: the disc's systems in registration order, then the transform, then the draw in order.
// The camera is cleared first, so a frame whose systems set none draws nothing. A system or the transform stops
// the frame at its first error; the draw returns the first failed put's code.
inline int ecs_world_frame(ecs_world &world, const ecs_frame &frame, const std::vector<ecs_id> &order)
{
    world.camera_set = false;
    for (const auto &entry : world.systems) {
        const int code = entry.second(world, frame);
        if (code != RV_OK) {
            return code;
        }
    }
    const int transformed = ecs_system_transform(world);
    if (transformed != RV_OK) {
        return transformed;
    }
    return ecs_system_draw(world, frame, order);
}

} // namespace rv_pdklib
