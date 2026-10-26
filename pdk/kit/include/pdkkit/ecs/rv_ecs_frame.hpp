#pragma once

// Frame of the scene ECS: the frame record, the registry of frame systems, the camera step, the draw system and the frame
// order: the disc's systems, the transform, the camera, the face camera, then the draw of every live entity in slot order.
// ecs_system_draw returns codes without logging. ecs_frame_run and ecs_world_frame log a failed system, camera, face camera,
// draw or unload by name, once per run of the same returned code: a repeat stays silent, and a frame that succeeds whole
// starts a new run. A std::exception thrown inside the draw is logged by ecs_guard on every frame, before that report.
// A frame that fails and also fails its unloads alternates the one record, so both repeat.
// ecs_system_transform logs each failure with its reason on every failing frame; the scene loader builds no broken chain.

#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <string_view>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/cv/rv_primitives.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_face_camera.hpp"
#include "pdkkit/ecs/rv_ecs_loads.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"
#include "pdklib/rv_gte/rv_gte_camera.hpp"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"

namespace rv_pdkkit
{

// Port 0 at the frame update: its state, and the buttons that went down since the previous update.
// The first update counts every button already held as pressed, as rv_cppdisc's MENU edge does.
struct ecs_input {
    rv_istate state{};
    uint64_t pressed = 0;
};

// Handle of one frame: the disc's drive, the clock and the input.
struct ecs_frame {
    rv_pdko *pdk = nullptr;
    // The clock: the number of updates with dt > 0.
    int64_t frame = 0;
    ecs_input input{};
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

// Copies the world matrix field by field: rv_cg_matrix has the same units, and no layout is assumed between the two types.
inline void ecs_world_to_cg(const rv_pdklib::rv_gte_matrix &m, rv_cg_matrix &out)
{
    for (std::size_t i = 0; i < std::size(out.r); ++i) {
        out.r[i] = m.r[i];
    }
    for (std::size_t i = 0; i < std::size(out.t); ++i) {
        out.t[i] = m.t[i];
    }
}

// Reason texts of the camera step's refusals; the frame logs them with the code.
inline constexpr const char *ecs_camera_none = "the scene has no lens entity";
inline constexpr const char *ecs_camera_many = "the scene has two or more lens entities";
inline constexpr const char *ecs_camera_no_place = "the lens entity has no world matrix";
inline constexpr const char *ecs_camera_screen = "the screen height is not in 1..INT32_MAX";
inline constexpr const char *ecs_camera_target = "the eye plus the aim leaves Q20.12";
inline constexpr const char *ecs_camera_make = "rv_gte_camera_make refused the lens and the world matrix";

// The lens entity: RV_ERR_NOENT without one, RV_ERR_ALREADY with two; reason names the refusal. Logs nothing.
[[nodiscard]] inline int ecs_camera_find(ecs_world &world, ecs_id &id, const ecs_component_lens *&lens, ecs_reason &reason)
{
    int count = 0;
    ecs_component_each<ecs_component_lens>(world, [&](ecs_id holder, ecs_component_lens &component) {
        ++count;
        id = holder;
        lens = &component;
    });
    if (count == 0) {
        ecs_reason_refuse(reason, ecs_camera_none);
        return RV_ERR_NOENT;
    }
    if (count > 1) {
        ecs_reason_refuse(reason, ecs_camera_many);
        return RV_ERR_ALREADY;
    }
    return RV_OK;
}

// The frame's camera from the lens entity: the eye is the world translation, the aim is the z column of the world
// rotation and the up is its y column, so the target is eye + aim. The world's camera and camera_set are written only on RV_OK;
// reason names a refusal.
[[nodiscard]] inline int ecs_system_camera(ecs_world &world, const ecs_frame &frame, ecs_reason &reason)
{
    auto &state = detail::ecs_state(world);
    ecs_id id{};
    const ecs_component_lens *lens = nullptr;
    const int found = ecs_camera_find(world, id, lens, reason);
    if (found != RV_OK) {
        return found;
    }
    const ecs_component_world *place = ecs_component_get<ecs_component_world>(world, id);
    if (place == nullptr) {
        ecs_reason_refuse(reason, ecs_camera_no_place);
        return RV_ERR_INVAL;
    }
    const int64_t screen_height = rv_cv_screen_height(rv_pdko_cv(frame.pdk));
    const bool screen_fits = screen_height > 0 && screen_height <= std::numeric_limits<int32_t>::max();
    if (!screen_fits) {
        ecs_reason_refuse(reason, ecs_camera_screen);
        return RV_ERR_INVAL;
    }
    const auto &m = place->matrix;
    const rv_pdklib::fx_vec3 eye = { m.t[0], m.t[1], m.t[2] };
    const rv_pdklib::fx_vec3 aim = { m.r[2], m.r[5], m.r[8] };
    const rv_pdklib::fx_vec3 up = { m.r[1], m.r[4], m.r[7] };
    rv_pdklib::fx_vec3 target = {};
    if (rv_pdklib::fx_vec3_add(eye, aim, target) != RV_OK) {
        ecs_reason_refuse(reason, ecs_camera_target);
        return RV_ERR_INVAL;
    }
    rv_pdklib::rv_gte_camera camera = {};
    const int make_code = rv_pdklib::rv_gte_camera_make(eye,
        target,
        up,
        lens->fov,
        static_cast<int32_t>(screen_height),
        lens->near_plane,
        lens->far_plane,
        camera);
    if (make_code != RV_OK) {
        ecs_reason_refuse(reason, ecs_camera_make);
        return make_code;
    }
    ecs_world_to_cg(camera.view, state.camera.view);
    state.camera.h = camera.h;
    state.camera.near_plane = camera.near_plane;
    state.camera.far_plane = camera.far_plane;
    state.camera_set = true;
    return RV_OK;
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
        // The name ends in a NUL, so the view's data() is a C string.
        const int64_t addr =
            rv_cd_resource_addr(rv_pdko_cd(frame.pdk), RV_CD_RESOURCE_MODEL, std::string_view{ model->name }.data());
        if (addr < 0) {
            return static_cast<int>(addr);
        }
        rv_cg_put put = {};
        ecs_world_to_cg(place->matrix, put.world);
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

// Rebuilds the world's draw_order: every live entity in slot index order. The draw skips those without a model.
[[nodiscard]] inline int ecs_draw_order_build(ecs_world &world)
{
    auto &state = detail::ecs_state(world);
    state.draw_order.clear();
    return ecs_entities_each(world, [&](ecs_id id) -> int {
        state.draw_order.push_back(id);
        return RV_OK;
    });
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
// A returned code with a reason text (a system's own refusal) logs the text after the code.
inline void ecs_frame_log_failure(std::string_view name, int code, const ecs_reason &reason)
{
    try {
        const bool has_text = reason.text[0] != '\0';
        if (reason.thrown) {
            RV_LOG_ERR(ecs_frame_tag, "system {} threw, rv_err {}: {}", name, code, std::string_view{ reason.text });
        } else if (has_text) {
            RV_LOG_ERR(ecs_frame_tag, "system {} returned rv_err {}: {}", name, code, std::string_view{ reason.text });
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
// compares against, and a frame that succeeds whole clears it.
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

// The disc's systems in registration order, the transform, the camera, the face camera, then the draw of every live entity
// in slot order. A system, the transform, the camera or the face camera stops the frame at its first error; the draw returns
// the first failed put's code. The camera's, the face camera's and the draw's failures are reported by name, as a system's is.
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
    const int transformed = ecs_system_transform(world);
    if (transformed != RV_OK) {
        return transformed;
    }
    ecs_reason camera_reason;
    const int camera = ecs_system_camera(world, frame, camera_reason);
    if (camera != RV_OK) {
        ecs_frame_report(world, "ecs_system_camera", camera, camera_reason);
        return camera;
    }
    ecs_reason face_reason;
    const int faced = ecs_system_face_camera(world, face_reason);
    if (faced != RV_OK) {
        ecs_frame_report(world, "ecs_system_face_camera", faced, face_reason);
        return faced;
    }
    const int built = ecs_draw_order_build(world);
    const int drawn = built == RV_OK ? ecs_system_draw(world, frame, state.draw_order) : built;
    if (drawn != RV_OK) {
        ecs_frame_report(world, "ecs_system_draw", drawn, ecs_reason{});
        return drawn;
    }
    return RV_OK;
}

// One frame. The camera is cleared first and set by the camera step each frame; a frame that fails before the draw puts
// nothing. The draw puts every live entity with a model, a system's entities included. A failed draw or unload is logged
// by name. Unloads asked for during the frame run after it, even when the frame failed; a failed frame's code is returned
// before theirs. A system's exception ends the frame with its code in ecs_frame_run; an exception from the transform, the
// camera, the face camera or the draw gets the ecs_guard code. in_frame is cleared by the scope before the guard returns.
[[nodiscard]] inline int ecs_world_frame(ecs_world &world, const ecs_frame &frame)
{
    auto &state = detail::ecs_state(world);
    state.camera_set = false;
    const int ran = ecs_guard([&] {
        const ecs_frame_scope scope{ world };
        return ecs_frame_run(world, frame);
    });
    const int unloaded = ecs_unload_pending(world);
    if (unloaded != RV_OK) {
        ecs_frame_report(world, "ecs_unload_pending", unloaded, ecs_reason{});
    }
    if (ran != RV_OK) {
        return ran;
    }
    if (unloaded != RV_OK) {
        return unloaded;
    }
    // Only a frame that succeeds whole, unloads included, clears the record: a repeat of a failure is logged once.
    state.logged_system = {};
    state.logged_code = RV_OK;
    state.logged_reason = {};
    return RV_OK;
}

} // namespace rv_pdkkit
