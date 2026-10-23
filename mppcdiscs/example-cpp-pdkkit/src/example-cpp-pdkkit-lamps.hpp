#pragma once

#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"

namespace example_cpp_pdkkit
{

// Light of a lamp entity. The scene gives no fields: the system fills them. light is the entity that draws the light,
// made by ecs_system_lamp_light as a child of the lamp; was_near is the reach on the previous frame.
struct ecs_component_lamp_light {
    rv_pdkkit::ecs_id light{};
    bool burns = false;
    bool was_near = false;
    bool warned = false;
};

// Zone of a lamp: the zone entity (ecs_component_zone) switches the lamp entity named by lamp when an entity enters it.
struct ecs_component_zone_lamp {
    char lamp[rv_pdkkit::ecs_value_capacity]{};
};

// One-frame event on a lamp whose light changed: the lamp system sets it, and removes it at the start of its next pass.
struct ecs_component_lamp_switched {
};

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

// Frame system: makes the light of each lamp that has none (hidden). Each approach of the eye within RV_LAMP_REACH
// toggles the light: the first approach turns it on, the next turns it off. A lamp that cannot be placed (no world
// matrix, no route keys, a Q20.12 failure) is warned once and stays dark; the others go on.
// Error: a lamp's light cannot be made or shown (the component's code); the frame stops there.
int ecs_system_lamp_light(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

// Draws the lights of the lamps. The world does not draw them: a system-made entity is outside every load. Runs
// after the world frame, when the systems have set the camera. Error: the code of ecs_system_draw.
int ecs_draw_lamps(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_lamp_light, "lamp_light");
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_lamp, "zone_lamp", PDKKIT_FIELD(lamp, string, ""));
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_lamp_switched, "lamp_switched");
