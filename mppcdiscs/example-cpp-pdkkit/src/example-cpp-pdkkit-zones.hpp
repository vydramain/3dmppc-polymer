#pragma once

#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace example_cpp_pdkkit
{

// Box of a zone entity: size is the full extent along the world axes, Q20.12 m; the centre is the entity's world position.
struct ecs_component_zone {
    // Default 1 m each, so an added zone is a usable box.
    rv_pdklib::fx_vec3 size{ rv_pdklib::fx_one, rv_pdklib::fx_one, rv_pdklib::fx_one };
    // Working state of the zone system, not a scene field: set once a zone with a size that is not positive is warned,
    // so the warning is logged once and not every frame.
    bool warned = false;
};

// One-frame event on a zone: the zone system sets it when an entity enters, and removes it at the start of its next pass.
struct ecs_component_zone_crossed {
    // The entity that entered.
    rv_pdkkit::ecs_id who{};
    // Its world position now minus its world position on the previous frame, Q20.12.
    rv_pdklib::fx_vec3 direction{};
};

// Frame system: removes last frame's zone_crossed, then sets one on each zone that a trail entered this frame. Runs after
// the route system, which moves the riders and sets their trails. Error: the code of the first step that fails (NOENT,
// INVAL, NOMEM); the rest of the frame is skipped. Not logged here: ecs_frame_report logs the name and code.
int ecs_system_zones(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone,
    "zone",
    PDKKIT_FIELD(size, fixed3, rv_pdklib::fx_one, rv_pdklib::fx_one, rv_pdklib::fx_one));
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_crossed, "zone_crossed");
