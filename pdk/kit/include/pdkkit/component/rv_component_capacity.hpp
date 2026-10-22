#pragma once

// Capacities shared by the scene records and the component descriptions. Kept apart so a description does not pull in
// the scene reader.

namespace rv_pdkkit
{

// Bytes per name, NUL included: the arrays of ecs_scene_object, ecs_scene_entity and ecs_scene_component. A longer
// name refuses the scene.
inline constexpr int ecs_name_capacity = 64;
// Bytes per component field value, NUL included.
inline constexpr int ecs_value_capacity = 64;
// Fields per component, the fixed size of ecs_scene_component: a component with more refuses the scene.
inline constexpr int ecs_component_field_capacity = 8;

} // namespace rv_pdkkit
