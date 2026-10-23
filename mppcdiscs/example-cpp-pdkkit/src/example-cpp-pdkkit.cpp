#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

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
// The lamp's component (lamp_light), and the name of its frame system.
constexpr const char *RV_EXAMPLE_CPP_LAMP_LIGHT = "lamp_light";
// The scene component of a board halo or letter: its description's name, and the name of its frame system.
constexpr const char *RV_EXAMPLE_CPP_FLICKER = "flicker";

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
        init_sound();
        return RV_OK;
    }

    void frame_render()
    {
        frame_begin(RV_EXAMPLE_CPP_BACKGROUND);
        play_sound();
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
    void log_draw_error(const char *step, int64_t code);
    void init_sound();
    void play_sound();
    void log_sound_error(int64_t code);

    // read_scene succeeded: the scene's route and board are in world_, so the disc draws and plays.
    bool scene_read_ = false;
    // The scene has a board entity (one with the board marker); the sound takes it into account.
    bool board_found_ = false;
    bool draw_error_logged_ = false;
    // The music and the clack; sound_ready_ is false when init_sound failed, and then nothing plays.
    rv_sound_state sound_state_;
    bool sound_ready_ = false;
    // Assets whose failure is logged already: one log per asset, so a later failure of another asset still shows.
    std::vector<std::string> sound_errors_logged_;
};

// Registers the route, board and lamp entity types, the route's frame system, then the lamp_light and flicker components
// with their frame systems, then the route_key, route_rider, trail, zone and zone_crossed components; a scene refers
// to them by name. The built-in components are registered by pdkkit_disc before this.
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
    const int lamp_system_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_LAMP_LIGHT, example_cpp_pdkkit::ecs_system_lamp_light);
    if (lamp_system_code != RV_OK) {
        return lamp_system_code;
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
    return RV_OK;
}

// Loads the scene into world_, then fills the route's keys and the board from its entities. The lamps' lights are made
// by the lamp_light system on the first frame. An error leaves them partly filled; the caller clears board_found_.
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
    const int frame_code = world_frame();
    if (frame_code != RV_OK) {
        log_draw_error("world", frame_code);
        return;
    }
    // The lights are drawn here: the world draws only the scene's entities, and the camera is the frame's.
    const int lamp_code = ecs_draw_lamps(world_, rv_pdkkit::ecs_frame{ pdk_, frame_, input_ });
    if (lamp_code != RV_OK) {
        log_draw_error("lamps", lamp_code);
    }
}

// Logs the first failure only: the same failure would repeat in every frame.
void rv_dmain::log_draw_error(const char *step, int64_t code)
{
    if (draw_error_logged_) {
        return;
    }
    draw_error_logged_ = true;
    RV_LOG_ERR("draw", "{} not drawn: rv_err {}", step, code);
}

// Loads the music and tabulates the areas; runs once, after read_scene found the camera keys.
void rv_dmain::init_sound()
{
    const ecs_component_route *route = rv_route_of(world_);
    if (route == nullptr) {
        RV_LOG_ERR("sound", "sound off: the scene has no route");
        return;
    }
    const int64_t sound_code = rv_sound_init(pdk_, route->keys, sound_state_);
    if (sound_code < 0) {
        RV_LOG_ERR("sound", "sound off: {} rv_err {}", sound_state_.error_asset, sound_code);
        return;
    }
    sound_ready_ = true;
}

// Plays the frame's sound; without init_sound there is no music, and init_sound has logged why.
void rv_dmain::play_sound()
{
    if (!sound_ready_) {
        return;
    }
    const int64_t sound_code = rv_sound_update(pdk_, frame_ % RV_ROUTE_PERIOD_FRAMES, board_found_, sound_state_);
    if (sound_code < 0) {
        log_sound_error(sound_code);
    }
}

// Logs the first failure of each asset only: the same failure would repeat in every frame.
void rv_dmain::log_sound_error(int64_t code)
{
    const std::string asset = sound_state_.error_asset;
    if (std::find(sound_errors_logged_.begin(), sound_errors_logged_.end(), asset) != sound_errors_logged_.end()) {
        return;
    }
    sound_errors_logged_.push_back(asset);
    RV_LOG_ERR("sound", "sound not played: {} rv_err {}", asset, code);
}

} // namespace example_cpp_pdkkit

RV_MPPC_DISC_ENTRY_DEF(example_cpp_pdkkit::rv_dmain);
