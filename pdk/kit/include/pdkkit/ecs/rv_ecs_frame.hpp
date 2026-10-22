#pragma once

// Frame of the scene ECS: the frame record, the registry of frame systems, the camera, the draw system and the frame order.
// Draw returns codes without logging: a frame repeats them. ecs_frame_run logs a failed system by name, returned or
// thrown, once per run of the same failure: a repeat stays silent, and a frame whose systems all succeed starts a new run.
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
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_loads.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"

namespace rv_pdkkit
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

// RV_ERR_INVAL for an empty name or a null system, RV_ERR_ALREADY when the name is taken, RV_ERR_NOMEM when the
// registry cannot grow (ecs_guard). The world is unchanged on failure.
[[nodiscard]] inline int ecs_system_register(ecs_world &world, std::string_view name, ecs_system run)
{
    const bool bad = name.empty() || run == nullptr;
    if (bad) {
        return RV_ERR_INVAL;
    }
    auto &state = detail::ecs_state(world);
    return ecs_guard([&] {
        for (const auto &entry : state.systems) {
            if (entry.first == name) {
                return RV_ERR_ALREADY;
            }
        }
        state.systems.emplace_back(name, run);
        return RV_OK;
    });
}

// Camera of the frame, for the puts that follow it. A system sets it; ecs_system_draw needs it.
inline void ecs_world_camera(ecs_world &world, const rv_cg_camera &camera)
{
    auto &state = detail::ecs_state(world);
    state.camera = camera;
    state.camera_set = true;
}

// Puts one entity's model. Dead, model-less and hidden entities are skipped; a decal takes the decal window.
// An entity with two_sided is drawn without back-face culling, so both faces of a flat card show.
[[nodiscard]] inline int ecs_draw_entity(ecs_world &world, const ecs_frame &frame, ecs_id id)
{
    return ecs_guard([&]() -> int {
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
        const bool two_sided = ecs_component_get<ecs_component_two_sided>(world, id) != nullptr;
        const uint32_t cull = two_sided ? RV_CG_CULL_NONE : RV_CG_CULL_BACK;
        const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(frame.pdk), RV_CD_RESOURCE_MODEL, model->name.c_str());
        if (addr < 0) {
            return static_cast<int>(addr);
        }
        rv_cg_put put = {};
        static_assert(sizeof(rv_pdklib::rv_mat4::m) == sizeof(rv_cg_put::world),
            "rv_mat4 and rv_cg_put.world hold the same 16 floats");
        std::memcpy(put.world, place->matrix.m, sizeof(put.world));
        put.depth_min = depth_min;
        put.depth_max = depth_max;
        put.cull = cull;
        put.tint = model->tint;
        const int64_t count = rv_cg_model_put(rv_pdko_cg(frame.pdk), addr, &put);
        if (count < 0) {
            return static_cast<int>(count);
        }
        return RV_OK;
    });
}

// Puts the models in the order given, inside the frame's camera. RV_ERR_NOENT without a camera: nothing is put.
// A failed entity does not stop the rest, as the disc draws on; the first failure's code is returned.
[[nodiscard]] inline int ecs_system_draw(ecs_world &world, const ecs_frame &frame, const std::vector<ecs_id> &order)
{
    auto &state = detail::ecs_state(world);
    return ecs_guard([&]() -> int {
        if (!state.camera_set) {
            return RV_ERR_NOENT;
        }
        const int64_t camera_code = rv_cg_frame_camera(rv_pdko_cg(frame.pdk), &state.camera);
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
    });
}

// Rebuilds the world's draw_order: the loads' entities in table order, each load in file order.
inline void ecs_draw_order_build(ecs_world &world)
{
    auto &state = detail::ecs_state(world);
    state.draw_order.clear();
    for (const ecs_load &load : state.loads) {
        state.draw_order.insert(state.draw_order.end(), load.made.begin(), load.made.end());
    }
}

// Sets in_frame for its scope, so an exception out of the frame still clears it.
struct ecs_frame_scope {
    ecs_world &world;

    explicit ecs_frame_scope(ecs_world &w)
        : world(w)
    {
        auto &state = detail::ecs_state(world);
        state.in_frame = true;
    }

    ~ecs_frame_scope()
    {
        auto &state = detail::ecs_state(world);
        state.in_frame = false;
    }
};

inline constexpr const char *ecs_frame_tag = "ecs-frame";

// Best effort, as ecs_guard_log: the line is lost when formatting throws, and the frame's code stands.
inline void ecs_frame_log_failure(std::string_view name, int code, const ecs_reason &reason)
{
    try {
        if (reason.thrown) {
            RV_LOG_ERR(ecs_frame_tag, "system {} threw, rv_err {}: {}", name, code, std::string_view{ reason.text });
        } else {
            RV_LOG_ERR(ecs_frame_tag, "system {} returned rv_err {}", name, code);
        }
    } catch (...) {
        // the line is lost
    }
}

inline bool ecs_reason_same(const ecs_reason &a, const ecs_reason &b)
{
    return a.thrown == b.thrown && std::strcmp(a.text, b.text) == 0;
}

// Logs a failed system unless it repeats the last report (system, code and reason): the record is what the next report
// compares against, and a frame whose systems all succeed clears it.
inline void ecs_frame_report(ecs_world &world, std::string_view name, int code, const ecs_reason &reason)
{
    auto &state = detail::ecs_state(world);
    const bool repeat =
        name == state.logged_system && code == state.logged_code && ecs_reason_same(reason, state.logged_reason);
    if (repeat) {
        return;
    }
    state.logged_system = name;
    state.logged_code = code;
    state.logged_reason = reason;
    ecs_frame_log_failure(name, code, reason);
}

// ecs_frame_run's exceptions are logged by ecs_frame_report from the copy in the reason, so the handler reports nothing.
inline void ecs_frame_no_report(const char *)
{
}

// The disc's systems in registration order, the transform, then the draw of the loads' entities.
// A system or the transform stops the frame at its first error; the draw returns the first failed put's code.
// A system's exception is mapped by ecs_try, so it becomes a code and goes through the same report as a returned error.
inline int ecs_frame_run(ecs_world &world, const ecs_frame &frame)
{
    auto &state = detail::ecs_state(world);
    for (const auto &entry : state.systems) {
        ecs_reason reason;
        const int code = ecs_try(
            [&] {
                return entry.second(world, frame);
            },
            reason,
            ecs_frame_no_report);
        if (code != RV_OK) {
            ecs_frame_report(world, entry.first, code, reason);
            return code;
        }
    }
    state.logged_system = {};
    state.logged_code = RV_OK;
    state.logged_reason = {};
    const int transformed = ecs_system_transform(world);
    if (transformed != RV_OK) {
        return transformed;
    }
    ecs_draw_order_build(world);
    return ecs_system_draw(world, frame, state.draw_order);
}

// One frame. The camera is cleared first, so a frame whose systems set none draws nothing. Draws only the entities
// of loads: entities a system creates outside a load are not drawn. Unloads asked for during the frame run after it,
// even when the frame failed; a failed frame's code is returned before theirs. A system's exception ends the frame with
// its code in ecs_frame_run; an exception from the transform or the draw gets the ecs_guard code. in_frame is cleared
// by the scope before the guard returns.
[[nodiscard]] inline int ecs_world_frame(ecs_world &world, const ecs_frame &frame)
{
    auto &state = detail::ecs_state(world);
    state.camera_set = false;
    const int ran = ecs_guard([&] {
        const ecs_frame_scope scope{ world };
        return ecs_frame_run(world, frame);
    });
    const int unloaded = ecs_unload_pending(world);
    const bool failed = ran != RV_OK;
    return failed ? ran : unloaded;
}

} // namespace rv_pdkkit
