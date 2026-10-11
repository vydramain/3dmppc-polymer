#pragma once

#include <cstdint>
#include <vector>

#include "pdk/cg/rv_cg.h"

namespace example_cpp
{

// One period of the demo's camera loop: 1080 frames = 18 s at 1/60 s per frame.
// The camera of a frame is a pure function of its number, so the period repeats exactly.
constexpr int64_t RV_ROUTE_PERIOD_FRAMES = 1080;

// One cam_NN key of the scene: where the camera stands and which way it looks.
struct rv_route_key {
    int32_t index; // NN of cam_NN: the key order on the route
    rv_cg_vec3 eye;
    rv_cg_vec3 dir; // unit length, see rv_route_unit
};

// True, with index = NN, when name is exactly cam_NN (two digits).
bool rv_route_key_name(const char *name, int32_t &index);

// v scaled to unit length; a zero-length v gives +z rather than a division by zero.
rv_cg_vec3 rv_route_unit(const rv_cg_vec3 &v);

// Camera of frame `frame` (any value; taken modulo the period). keys must be non-empty and sorted by index.
rv_cg_camera rv_route_camera_at(const std::vector<rv_route_key> &keys, int64_t frame);

} // namespace example_cpp
