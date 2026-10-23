#pragma once

#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"

namespace example_cpp_pdkkit
{

// Working state of a lamp: on is the light, false is dark. The scene gives no fields: the lamps system sets it.
struct ecs_component_lamp_light {
    bool on = false;
};

// Zone of a lamp: the zone entity (ecs_component_zone) switches the lamp entity named by lamp when an entity enters it.
struct ecs_component_zone_lamp {
    char lamp[rv_pdkkit::ecs_value_capacity]{};
    // Working state of the lamps system, not a scene field: set once a zone whose lamp is not found is warned.
    bool warned = false;
};

// One-frame event on a lamp whose light changed: the lamps system sets it, and removes it at the start of its next pass.
struct ecs_component_lamp_switched {};

// Scene entity type "lamp", used by the scene objects with entity = "lamp": they get the lamp_light component when created.
class ecs_entity_lamp : public rv_pdkkit::ecs_entity
{
public:
    ecs_entity_lamp()
        : rv_pdkkit::ecs_entity("lamp")
    {
    }

    int add_defaults(rv_pdkkit::ecs_world &world, rv_pdkkit::ecs_id id) const override;
};

// Frame system: removes last frame's lamp_switched, then switches the lamp of each zone_crossed whose zone has a zone_lamp:
// a step along +X lights it, along -X darkens it. A lamp that is not found is warned once and the zone switches nothing.
// Error: the code of the first step that fails (NOENT for a named entity without lamp_light); the rest of the frame is skipped.
int ecs_system_lamps(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

// Frame system: on each lamp with lamp_switched, shows or hides its children (its light parts): hidden while the lamp is
// dark, shown while it is lit. Runs after the lamps system. Error: the code of the first step that fails.
int ecs_system_light(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_lamp_light, "lamp_light");
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_lamp, "zone_lamp", PDKKIT_FIELD(lamp, string, ""));
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_lamp_switched, "lamp_switched");
