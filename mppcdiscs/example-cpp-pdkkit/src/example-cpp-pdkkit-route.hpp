#pragma once

#include <cstdint>

#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace example_cpp_pdkkit
{

// One period of the demo's camera loop: 8640 frames = 144 s at 1/60 s per frame.
// The rider pose of a frame is a pure function of its number, so the period repeats exactly.
constexpr int64_t RV_ROUTE_PERIOD_FRAMES = 8640;

// One route_key of the scene: its local transform, read by the rider system in Q20.12; rotation in degrees.
struct rv_route_key {
    int32_t index; // order of its route_key
    rv_pdklib::fx_vec3 position;
    rv_pdklib::fx_vec3 rotation; // degrees, Q20.12
};

// Marks an entity as a route key; order is its place on the route, the scene sets it.
struct ecs_component_route_key {
    int32_t order = 0;
};

// Marks the entity the route system moves (the camera eye); the scene sets it.
struct ecs_component_route_rider {
};

// Rider's trail, written by the route system; the zones read it. moved is false in the frame the trail was added and
// true in every frame after it.
struct ecs_component_trail {
    rv_pdklib::fx_vec3 prev{}; // previous world position, Q20.12
    bool moved = false;        // false only in the frame the trail was added; not a test of movement
};

// Frame system: moves each route_rider along the route keys (local transform, Catmull-Rom by the frame's clock).
// No rider is not an error. Error: the code of the first step that fails (ALREADY, INVAL, NOMEM, NOENT for no
// route_key). Not logged here: ecs_frame_report logs the system's name and code, and a repeat of the same code stays silent.
int ecs_system_route(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_route_key, "route_key", PDKKIT_FIELD(order, int32, 0));
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_route_rider, "route_rider");
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_trail, "trail");
