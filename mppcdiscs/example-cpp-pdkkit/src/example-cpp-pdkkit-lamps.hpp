#pragma once

#include <cstdint>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "example-cpp-pdkkit-route.hpp"

namespace example_cpp_pdkkit
{

// Light of a lamp_N_light object: the frames of the route period in which it burns, [on_frame, off_frame).
// Both 0 means it never burns. rv_lamps_schedule fills it.
struct ecs_component_lamp_light {
    int64_t on_frame = 0;
    int64_t off_frame = 0;
};

// Factory of lamp_light: it has no fields, so any field is RV_ERR_INVAL.
int ecs_lamp_light_factory(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id, const rv_cd_scene_component &component);

// Frame system: shows each lamp_light entity that burns at the frame's place in the period, hides the others.
// Error: RV_ERR_NOENT when a dark lamp cannot take its hidden mark (a stale id). Not logged.
int ecs_system_lamp_light(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

// Sets on_frame and off_frame of each lamp_light entity from the world matrix of its parent (lamp_N) and the route
// keys (sorted by index). A lamp the route does not pass in both halves of the period stays dark and gets one warning.
// Returns RV_OK or the first error (a base leaves Q20.12, the route math fails, or a parent lacks a name or a world
// matrix); the lamps after it keep their schedule.
int rv_lamps_schedule(rv_pdkkit::ecs_world &world, const std::vector<rv_route_key> &keys);

} // namespace example_cpp_pdkkit
