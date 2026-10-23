#pragma once

#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace example_cpp_pdkkit
{

// Box of a zone entity: size is the full extent along the world axes, Q20.12 m; the centre is the entity's world position.
struct ecs_component_zone {
    // Default 1 m each, so an added zone is a usable box.
    rv_pdklib::fx_vec3 size{ rv_pdklib::fx_one, rv_pdklib::fx_one, rv_pdklib::fx_one };
};

// One-frame event on a zone: the zone system sets it when an entity enters, and removes it at the start of its next pass.
struct ecs_component_zone_crossed {
    // The entity that entered.
    rv_pdkkit::ecs_id who{};
    // Its world position now minus its world position on the previous frame, Q20.12.
    rv_pdklib::fx_vec3 direction{};
};

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone,
    "zone",
    PDKKIT_FIELD(size, fixed3, rv_pdklib::fx_one, rv_pdklib::fx_one, rv_pdklib::fx_one));
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_crossed, "zone_crossed");
