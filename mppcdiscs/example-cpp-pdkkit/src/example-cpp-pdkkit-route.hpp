#pragma once

#include <cstdint>
#include <vector>

#include "pdk/cg/rv_cg.h"
#include "pdklib/rv_ecs/rv_ecs.hpp"
#include "pdklib/rv_ecs/rv_ecs_entity.hpp"
#include "pdklib/rv_ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace example_cpp_pdkkit
{

// One period of the demo's camera loop: 4320 frames = 72 s at 1/60 s per frame.
// The camera of a frame is a pure function of its number, so the period repeats exactly.
constexpr int64_t RV_ROUTE_PERIOD_FRAMES = 4320;

// One cam_NN key of the scene: where the camera stands and which way it looks, in Q20.12.
struct rv_route_key {
    int32_t index; // NN of cam_NN: the key order on the route
    rv_pdklib::fx_vec3 eye;
    rv_pdklib::fx_vec3 dir; // unit length, see rv_route_unit
};

// True, with index = NN, when name is exactly cam_NN (two digits).
bool rv_route_key_name(const char *name, int32_t &index);

// v scaled to unit length in Q20.12; a zero-length v gives +z rather than a division by zero.
// Error: the result leaves Q20.12; out is untouched.
int rv_route_unit(rv_pdklib::fx_vec3 v, rv_pdklib::fx_vec3 &out);

// Eye of frame `frame` in Q20.12, on the same spline of eyes as rv_route_camera_at.
// Error: keys empty (RV_ERR_NOENT), or the Q20.12 math leaves its range (RV_ERR_INVAL); out is untouched.
int rv_route_eye_at(const std::vector<rv_route_key> &keys, int64_t frame, rv_pdklib::fx_vec3 &out);

// Camera of frame `frame` (any value, negative too; taken modulo the period). keys must be sorted by index.
// Error: keys empty (RV_ERR_NOENT), or the Q20.12 math leaves its range; out is untouched.
int rv_route_camera_at(const std::vector<rv_route_key> &keys, int64_t frame, rv_cg_camera &out);

// The route entity's component: the keys of its cam_NN children, sorted by index. rv_route_collect fills it.
struct ecs_component_route {
    std::vector<rv_route_key> keys;
};

// Scene entity type "route": a group that gets the route component when it is created.
class ecs_entity_route : public rv_pdklib::ecs_entity
{
public:
    ecs_entity_route()
        : rv_pdklib::ecs_entity("route")
    {
    }

    int add_defaults(rv_pdklib::ecs_world &world, rv_pdklib::ecs_id id) const override;
};

// Fills the route's keys from its cam_NN children and copies them to keys. Call after ecs_system_transform.
// Error: RV_ERR_NOENT without one route entity or without a key, RV_ERR_ALREADY for two route entities,
// RV_ERR_INVAL for a Q20.12 failure or a camera without a world matrix. Logged here; keys is untouched.
int rv_route_collect(rv_pdklib::ecs_world &world, std::vector<rv_route_key> &keys);

// Frame system: sets the frame's camera from the route keys. Error: RV_ERR_NOENT without a route entity,
// else the code of rv_route_camera_at. Not logged: a frame repeats it, so the disc logs the first one.
int ecs_system_route(rv_pdklib::ecs_world &world, const rv_pdklib::ecs_frame &frame);

} // namespace example_cpp_pdkkit
