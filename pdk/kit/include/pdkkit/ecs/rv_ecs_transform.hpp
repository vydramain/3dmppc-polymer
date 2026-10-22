#pragma once

// Transform system of the scene ECS: the world matrix of each entity, the float boundary with rv_cg (rv_cg_put.world),
// for drawing only. Same order of operations as rv_scene_world, so the matrices match byte for byte in one build.

#include <cstdint>
#include <vector>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_math.hpp"
#include "pdklib/rv_scene/rv_scene_space.hpp"

namespace rv_pdkkit
{

inline constexpr const char *ecs_transform_tag = "ecs-transform";

// Scene-space matrix of an entity; written by ecs_system_transform.
struct ecs_component_world {
    rv_pdklib::rv_mat4 matrix{};
};

// Scene-space matrix of the entity with a transform: its local matrix, then each ancestor's applied outside it.
// RV_ERR_INVAL for a missing or dead parent, or a chain longer than the live count (a cycle).
// The logs are inside the guard too: a throwing log line becomes the guard's code.
[[nodiscard]] inline int ecs_transform_world(ecs_world &world, ecs_id id, rv_pdklib::rv_mat4 &out)
{
    const auto &state = detail::ecs_state(world);
    return ecs_guard([&]() -> int {
        const ecs_component_transform *self = ecs_component_get<ecs_component_transform>(world, id);
        if (self == nullptr) {
            RV_LOG_ERR(ecs_transform_tag, "entity {}:{} has no transform", id.index, id.generation);
            return RV_ERR_INVAL;
        }
        rv_pdklib::rv_mat4 m = rv_pdklib::rv_scene_local_trs(self->position, self->rotation, self->scale);
        ecs_id parent = self->parent;
        // An acyclic chain is shorter than live_count; reaching live_count steps means a cycle.
        for (uint32_t steps = 0; parent != ecs_id{}; ++steps) {
            const bool cycle = steps >= state.live_count;
            if (cycle) {
                RV_LOG_ERR(ecs_transform_tag, "entity {}:{}: parent chain is a cycle", id.index, id.generation);
                return RV_ERR_INVAL;
            }
            const ecs_component_transform *up = ecs_component_get<ecs_component_transform>(world, parent);
            if (up == nullptr) {
                RV_LOG_ERR(ecs_transform_tag,
                    "entity {}:{}: parent {}:{} is dead or has no transform",
                    id.index,
                    id.generation,
                    parent.index,
                    parent.generation);
                return RV_ERR_INVAL;
            }
            m = rv_pdklib::rv_mat4_mul(rv_pdklib::rv_scene_local_trs(up->position, up->rotation, up->scale), m);
            parent = up->parent;
        }
        out = m;
        return RV_OK;
    });
}

// Writes ecs_component_world of every entity with a transform. Entities are listed first: a component cannot be added
// inside ecs_component_each. Stops at the first failing entity; matrices written before it stay.
// RV_ERR_NOMEM from the holder list leaves the world unchanged; NOMEM from ecs_component_ensure leaves the matrices
// written before it.
[[nodiscard]] inline int ecs_system_transform(ecs_world &world)
{
    return ecs_guard([&]() -> int {
        std::vector<ecs_id> holders;
        ecs_component_each<ecs_component_transform>(world, [&holders](ecs_id id, ecs_component_transform &) {
            holders.push_back(id);
        });
        for (const ecs_id id : holders) {
            rv_pdklib::rv_mat4 matrix{};
            const int computed = ecs_transform_world(world, id, matrix);
            if (computed != RV_OK) {
                return computed;
            }
            ecs_component_world *out = nullptr;
            const int ensured = ecs_component_ensure<ecs_component_world>(world, id, out);
            if (ensured != RV_OK) {
                RV_LOG_ERR(ecs_transform_tag,
                    "entity {}:{} world component not set: rv_err {}",
                    id.index,
                    id.generation,
                    ensured);
                return ensured;
            }
            out->matrix = matrix;
        }
        return RV_OK;
    });
}

} // namespace rv_pdkkit
