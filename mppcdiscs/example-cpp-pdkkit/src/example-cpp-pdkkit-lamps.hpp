#pragma once

#include <cstdint>

#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"

namespace example_cpp_pdkkit
{

// Light of a lamp_N_light object: the frames of the route period in which it burns, [on_frame, off_frame).
// The scene gives no fields for these: the system fills them. Both 0 means it never burns.
// scheduled is set once the lamp has been tried, whether or not it burns; it is not a scene field.
struct ecs_component_lamp_light {
    int64_t on_frame = 0;
    int64_t off_frame = 0;
    bool scheduled = false;
};

// Frame system: schedules each lamp_light entity not yet scheduled from the route keys (sorted by index) and the world
// matrix of its parent (lamp_N). A lamp the route does not pass in both halves of the period stays dark. A lamp that
// cannot be scheduled (no parent with a name and a world matrix, a base or offset leaving Q20.12) is warned once and
// stays dark. Without route keys the lamps stay unscheduled and dark: unreachable while rv_route_collect refuses such
// a scene at load. Then shows each lamp that burns at the frame's place in the period, hides the others.
// Error: RV_ERR_NOENT when a dark lamp cannot take its hidden mark (a stale id). Not logged.
int ecs_system_lamp_light(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_lamp_light, "lamp_light");
