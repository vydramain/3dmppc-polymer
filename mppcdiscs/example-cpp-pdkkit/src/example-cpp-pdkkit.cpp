#include <cstdint>

#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_cppdisc/rv_cppdisc.hpp"
#include "pdkkit/component/rv_component_register.hpp"
#include "pdkkit/disc/rv_pdkkit_disc.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_loads.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "example-cpp-pdkkit-board.hpp"
#include "example-cpp-pdkkit-lamps.hpp"
#include "example-cpp-pdkkit-route.hpp"
#include "example-cpp-pdkkit-sound.hpp"
#include "example-cpp-pdkkit-zones.hpp"

namespace example_cpp_pdkkit
{

namespace
{

// The scene this disc draws, as scenes/main.scene.toml lands on the disc.
constexpr const char *RV_EXAMPLE_CPP_SCENE_NAME = "main.scene.toml";
// The frame system that sets the camera from the route keys.
constexpr const char *RV_EXAMPLE_CPP_ROUTE_SYSTEM = "route";
// The frame systems that switch the lamps on the zones' entries, then show the lamps' light parts; they run after zones.
constexpr const char *RV_EXAMPLE_CPP_LAMPS_SYSTEM = "lamps";
constexpr const char *RV_EXAMPLE_CPP_LIGHT_SYSTEM = "light";
// The scene component of a board halo or letter: its description's name, and the name of its frame system.
constexpr const char *RV_EXAMPLE_CPP_FLICKER = "flicker";
// The frame system that raises zone_crossed on the zones the riders enter; it runs after the route system.
constexpr const char *RV_EXAMPLE_CPP_ZONES_SYSTEM = "zones";
// The frame system that plays the listener's music and loop on the zones' zone_crossed; it runs after zones.
constexpr const char *RV_EXAMPLE_CPP_MUSIC_SYSTEM = "music";

constexpr rv_color RV_EXAMPLE_CPP_BACKGROUND = { 20, 24, 40 };

// The route's, the board's and the lamp's entity types: the world keeps pointers to them, so they have static storage.
const ecs_entity_route route_type;
const ecs_entity_board board_type;
const ecs_entity_lamp lamp_type;

} // namespace

class rv_dmain : public rv_pdkkit::pdkkit_disc
{
public:
    int64_t disc_initialize(rv_pdko *pdk)
    {
        const int64_t base_result = pdkkit_disc::disc_initialize(pdk);
        if (base_result != RV_OK) {
            return base_result;
        }

        // No scene, or one that does not read: the disc runs without it and says why.
        const int read_code = read_scene();
        if (read_code != RV_OK) {
            scene_read_ = false;
            board_found_ = false;
            RV_LOG_ERR("scene", "{} not used: rv_err {}", RV_EXAMPLE_CPP_SCENE_NAME, read_code);
            return RV_OK;
        }
        scene_read_ = true;
        if (!board_found_) {
            RV_LOG_WARN("scene", "no board entity: the board has no lettering");
        }
        return RV_OK;
    }

    void frame_render()
    {
        frame_begin(RV_EXAMPLE_CPP_BACKGROUND);
        draw_world();
        frame_end();
    }

    void disc_shutdown()
    {
        // Nothing to release: the drive frees the scene and models when this disc unloads.
    }

    const char *disc_title() const
    {
        return "example-cpp-pdkkit";
    }

private:
    int read_scene();
    int register_ecs();
    int collect_parts(uint32_t load_number);
    void draw_world();

    // read_scene succeeded: the scene's route and board are in world_, so the disc draws.
    bool scene_read_ = false;
    // The scene has a board entity (one with the board marker); the disc warns when it has none.
    bool board_found_ = false;
};

// Registers the route, board and lamp entity types, the route's frame system, then the lamp_light and flicker components
// with the flicker frame system, then the route_key, route_rider, trail, zone, zone_crossed, zone_lamp, lamp_switched,
// zone_music, zone_clack and listener components, then the zones, music, lamps and light frame systems in this order,
// which is the frame's order; a scene refers to them by name. pdkkit_disc registers the built-in components before this.
int rv_dmain::register_ecs()
{
    const int type_code = rv_pdkkit::ecs_entity_register(world_, route_type);
    if (type_code != RV_OK) {
        return type_code;
    }
    const int board_type_code = rv_pdkkit::ecs_entity_register(world_, board_type);
    if (board_type_code != RV_OK) {
        return board_type_code;
    }
    const int lamp_type_code = rv_pdkkit::ecs_entity_register(world_, lamp_type);
    if (lamp_type_code != RV_OK) {
        return lamp_type_code;
    }
    const int system_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_ROUTE_SYSTEM, example_cpp_pdkkit::ecs_system_route);
    if (system_code != RV_OK) {
        return system_code;
    }
    const int lamp_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_lamp_light>(world_);
    if (lamp_code != RV_OK) {
        return lamp_code;
    }
    const int flicker_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_flicker>(world_);
    if (flicker_code != RV_OK) {
        return flicker_code;
    }
    const int flicker_system_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_FLICKER, example_cpp_pdkkit::ecs_system_flicker);
    if (flicker_system_code != RV_OK) {
        return flicker_system_code;
    }
    const int route_key_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_route_key>(world_);
    if (route_key_code != RV_OK) {
        return route_key_code;
    }
    const int route_rider_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_route_rider>(world_);
    if (route_rider_code != RV_OK) {
        return route_rider_code;
    }
    const int trail_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_trail>(world_);
    if (trail_code != RV_OK) {
        return trail_code;
    }
    const int zone_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_zone>(world_);
    if (zone_code != RV_OK) {
        return zone_code;
    }
    const int zone_crossed_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_zone_crossed>(world_);
    if (zone_crossed_code != RV_OK) {
        return zone_crossed_code;
    }
    const int zone_lamp_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_zone_lamp>(world_);
    if (zone_lamp_code != RV_OK) {
        return zone_lamp_code;
    }
    const int lamp_switched_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_lamp_switched>(world_);
    if (lamp_switched_code != RV_OK) {
        return lamp_switched_code;
    }
    const int zone_music_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_zone_music>(world_);
    if (zone_music_code != RV_OK) {
        return zone_music_code;
    }
    const int zone_clack_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_zone_clack>(world_);
    if (zone_clack_code != RV_OK) {
        return zone_clack_code;
    }
    const int listener_code = rv_pdkkit::ecs_component_register<example_cpp_pdkkit::ecs_component_listener>(world_);
    if (listener_code != RV_OK) {
        return listener_code;
    }
    const int zones_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_ZONES_SYSTEM, example_cpp_pdkkit::ecs_system_zones);
    if (zones_code != RV_OK) {
        return zones_code;
    }
    const int music_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_MUSIC_SYSTEM, example_cpp_pdkkit::ecs_system_music);
    if (music_code != RV_OK) {
        return music_code;
    }
    const int lamps_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_LAMPS_SYSTEM, example_cpp_pdkkit::ecs_system_lamps);
    if (lamps_code != RV_OK) {
        return lamps_code;
    }
    const int light_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_LIGHT_SYSTEM, example_cpp_pdkkit::ecs_system_light);
    if (light_code != RV_OK) {
        return light_code;
    }
    return RV_OK;
}

// Loads the scene into world_, then fills the route's keys and the board from its entities. The lamps' light parts are
// scene entities, hidden until the light system shows them. An error leaves them partly filled; the caller clears board_found_.
int rv_dmain::read_scene()
{
    const int register_code = register_ecs();
    if (register_code != RV_OK) {
        return register_code;
    }
    uint32_t load_number = 0;
    const int load_code = scene_load(RV_EXAMPLE_CPP_SCENE_NAME, load_number);
    if (load_code != RV_OK) {
        return load_code;
    }
    const int transform_code = rv_pdkkit::ecs_system_transform(world_);
    if (transform_code != RV_OK) {
        return transform_code;
    }
    const int route_code = rv_route_collect(world_);
    if (route_code != RV_OK) {
        return route_code;
    }
    const int parts_code = collect_parts(load_number);
    if (parts_code != RV_OK) {
        return parts_code;
    }
    return RV_OK;
}

// Refuses the scene with a nameless entity of load load_number; then sets board_found_ from the board entities.
int rv_dmain::collect_parts(uint32_t load_number)
{
    const auto check_name = [this](rv_pdkkit::ecs_id id) -> int {
        const auto *name = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_name>(world_, id);
        if (name == nullptr) {
            RV_LOG_ERR("scene", "scene refused: an entity has no name");
            return RV_ERR_INVAL;
        }
        return RV_OK;
    };
    const int walk_code = rv_pdkkit::ecs_scene_entities(world_, load_number, check_name);
    if (walk_code != RV_OK) {
        return walk_code;
    }
    rv_pdkkit::ecs_component_each<ecs_component_board>(world_, [this](rv_pdkkit::ecs_id, ecs_component_board &) {
        board_found_ = true;
    });
    return RV_OK;
}

void rv_dmain::draw_world()
{
    // Without a read scene there is no route: disc_initialize has already logged why, so nothing is drawn.
    if (!scene_read_) {
        return;
    }
    // The kit draws every live entity, the lights included; it logs a failed system, face camera, draw or unload by name,
    // once per run of the same failure.
    world_frame();
}

} // namespace example_cpp_pdkkit

RV_MPPC_DISC_ENTRY_DEF(example_cpp_pdkkit::rv_dmain);
