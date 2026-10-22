#pragma once

// Transform system of the scene ECS: the world matrix of each entity, an integer rv_gte_matrix. The conversion to
// rv_cg is in draw (rv_ecs_frame.hpp), not here. Same order as rv_gte_trs, T * Ry * Rx * Rz * S, and the same parent
// chain: each ancestor's local matrix is applied outside the entity's.

#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_store.hpp"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_trs.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_pdkkit
{

inline constexpr const char *ecs_transform_tag = "ecs-transform";

// World matrix of an entity, integer; written by ecs_system_transform.
struct ecs_component_world {
    rv_pdklib::rv_gte_matrix matrix{};
};

// The entity's name for a log line, or index:generation when it has none.
[[nodiscard]] inline std::string ecs_transform_label(const ecs_world &world, ecs_id id)
{
    const ecs_component_name *named = ecs_component_get<ecs_component_name>(world, id);
    const bool has_name = named != nullptr && named->text[0] != '\0';
    return has_name ? std::string{ named->text } : std::format("{}:{}", id.index, id.generation);
}

// Local matrix of the transform of entity id. A refusal logs id and returns RV_ERR_INVAL.
[[nodiscard]] inline int
ecs_transform_local(const ecs_world &world, ecs_id id, const ecs_component_transform &t, rv_pdklib::rv_gte_matrix &out)
{
    if (rv_pdklib::rv_gte_trs(t.position, t.rotation, t.scale, out) != RV_OK) {
        RV_LOG_ERR(ecs_transform_tag, "entity {}: |R*S| reaches 8", ecs_transform_label(world, id));
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// World matrix of the entity with a transform: its local matrix, then each ancestor's applied outside it.
// The chain follows ecs_component_parent. An ancestor without a transform is an identity; its parent link still counts.
// RV_ERR_INVAL for a dead parent, or a chain longer than the live count (a cycle), or a matrix out of range.
// The logs are inside the guard too: a throwing log line becomes the guard's code.
[[nodiscard]] inline int ecs_transform_world(ecs_world &world, ecs_id id, rv_pdklib::rv_gte_matrix &out)
{
    const auto &state = detail::ecs_state(world);
    return ecs_guard([&]() -> int {
        const ecs_component_transform *self = ecs_component_get<ecs_component_transform>(world, id);
        if (self == nullptr) {
            RV_LOG_ERR(ecs_transform_tag, "entity {}:{} has no transform", id.index, id.generation);
            return RV_ERR_INVAL;
        }
        rv_pdklib::rv_gte_matrix m{};
        if (ecs_transform_local(world, id, *self, m) != RV_OK) {
            return RV_ERR_INVAL;
        }
        const ecs_component_parent *link = ecs_component_get<ecs_component_parent>(world, id);
        ecs_id parent = link == nullptr ? ecs_id{} : link->parent;
        // An acyclic chain is shorter than live_count; reaching live_count steps means a cycle.
        for (uint32_t steps = 0; parent != ecs_id{}; ++steps) {
            const bool cycle = steps >= state.live_count;
            if (cycle) {
                RV_LOG_ERR(ecs_transform_tag, "entity {}:{}: parent chain is a cycle", id.index, id.generation);
                return RV_ERR_INVAL;
            }
            const bool dead = !ecs_entity_alive(world, parent);
            if (dead) {
                RV_LOG_ERR(ecs_transform_tag,
                    "entity {}:{}: parent {}:{} is dead",
                    id.index,
                    id.generation,
                    parent.index,
                    parent.generation);
                return RV_ERR_INVAL;
            }
            const ecs_component_transform *up = ecs_component_get<ecs_component_transform>(world, parent);
            const bool has_transform = up != nullptr;
            if (has_transform) {
                rv_pdklib::rv_gte_matrix up_local{};
                if (ecs_transform_local(world, parent, *up, up_local) != RV_OK) {
                    return RV_ERR_INVAL;
                }
                rv_pdklib::rv_gte_matrix next{};
                if (rv_pdklib::rv_gte_compose(up_local, m, next) != RV_OK) {
                    RV_LOG_ERR(ecs_transform_tag,
                        "entity {}: the composed matrix leaves its range",
                        ecs_transform_label(world, parent));
                    return RV_ERR_INVAL;
                }
                m = next;
            }
            const ecs_component_parent *up_link = ecs_component_get<ecs_component_parent>(world, parent);
            parent = up_link == nullptr ? ecs_id{} : up_link->parent;
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
            rv_pdklib::rv_gte_matrix matrix{};
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
