#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_cppdisc/rv_cppdisc.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/ecs/rv_ecs_entity.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdkkit/ecs/rv_ecs_loads.hpp"
#include "pdkkit/ecs/rv_ecs_transform.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_scene/rv_scene_unit.hpp"
#include "example-cpp-pdkkit-board.hpp"
#include "example-cpp-pdkkit-lamps.hpp"
#include "example-cpp-pdkkit-route.hpp"
#include "example-cpp-pdkkit-sound.hpp"

namespace example_cpp_pdkkit
{

namespace
{

// The scene this disc draws, as scenes/main.scene.toml lands on the disc.
constexpr const char *RV_EXAMPLE_CPP_SCENE_NAME = "main.scene.toml";
// The scene object that carries the factory board lettering.
constexpr const char *RV_EXAMPLE_CPP_BOARD_NAME = "board";
// The frame system that sets the camera from the route keys.
constexpr const char *RV_EXAMPLE_CPP_ROUTE_SYSTEM = "route";
// The scene component of a lamp_N_light object; its factory and frame system are registered under this name.
constexpr const char *RV_EXAMPLE_CPP_LAMP_LIGHT = "lamp_light";
// The scene component of a board halo or letter; its factory and frame system are registered under this name.
constexpr const char *RV_EXAMPLE_CPP_FLICKER = "flicker";

constexpr rv_color RV_EXAMPLE_CPP_BACKGROUND = { 20, 24, 40 };

} // namespace

// rv_dmain_base_ carries everything pdklib/rv_cppdisc/rv_cppdisc.hpp
// considers the same for any C++ disc: the startup guards, MENU-button
// press-edge tracking, read_asset() and the frame plumbing.
RV_MPPC_DISC_CPP_DEF(rv_dmain_base_)

class rv_dmain : public rv_dmain_base_
{
public:
    int64_t disc_initialize(rv_pdko *pdk)
    {
        const int64_t base_result = rv_dmain_base_::disc_initialize(pdk);
        if (base_result < 0) {
            return base_result;
        }

        // No scene, or one that does not read: the disc runs without it and says why.
        const int read_code = read_scene();
        if (read_code != RV_OK) {
            keys_.clear();
            board_found_ = false;
            RV_LOG_ERR("scene", "{} not used: rv_err {}", RV_EXAMPLE_CPP_SCENE_NAME, read_code);
            return RV_OK;
        }
        if (!board_found_) {
            RV_LOG_WARN("scene", "no '{}' object: the board has no lettering", RV_EXAMPLE_CPP_BOARD_NAME);
        }
        init_sound();
        return RV_OK;
    }

    void frame_update(float dt)
    {
        rv_dmain_base_::frame_update(dt);
        if (dt > 0.0f) {
            ++frame_count_;
        }
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
    int collect_parts();
    void draw_world();
    void log_draw_error(const char *step, int64_t code);
    void init_sound();
    void play_sound();
    void log_sound_error(int64_t code);

    // Declared before world_: the world keeps a pointer to the type, so the type must outlive it.
    example_cpp_pdkkit::ecs_entity_route route_type_;
    rv_pdkkit::ecs_world world_;
    // The cam_NN keys, in NN order.
    std::vector<rv_route_key> keys_;
    // The scene has the board object; the sound takes it into account.
    bool board_found_ = false;
    // Frames updated while running: the demo's clock.
    int64_t frame_count_ = 0;
    bool draw_error_logged_ = false;
    // The music and the clack; sound_ready_ is false when init_sound failed, and then nothing plays.
    rv_sound_state sound_state_;
    bool sound_ready_ = false;
    // Assets whose failure is logged already: one log per asset, so a later failure of another asset still shows.
    std::vector<std::string> sound_errors_logged_;
};

// Registers the route's entity type and frame system, and the lamp_light and flicker components with their frame
// systems; a scene refers to them by name.
int rv_dmain::register_ecs()
{
    const int type_code = rv_pdkkit::ecs_entity_register(world_, route_type_);
    if (type_code != RV_OK) {
        return type_code;
    }
    const int system_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_ROUTE_SYSTEM, example_cpp_pdkkit::ecs_system_route);
    if (system_code != RV_OK) {
        return system_code;
    }
    const int lamp_factory_code =
        rv_pdkkit::ecs_component_register(world_, RV_EXAMPLE_CPP_LAMP_LIGHT, example_cpp_pdkkit::ecs_lamp_light_factory);
    if (lamp_factory_code != RV_OK) {
        return lamp_factory_code;
    }
    const int lamp_system_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_LAMP_LIGHT, example_cpp_pdkkit::ecs_system_lamp_light);
    if (lamp_system_code != RV_OK) {
        return lamp_system_code;
    }
    const int flicker_factory_code =
        rv_pdkkit::ecs_component_register(world_, RV_EXAMPLE_CPP_FLICKER, example_cpp_pdkkit::ecs_flicker_factory);
    if (flicker_factory_code != RV_OK) {
        return flicker_factory_code;
    }
    const int flicker_system_code =
        rv_pdkkit::ecs_system_register(world_, RV_EXAMPLE_CPP_FLICKER, example_cpp_pdkkit::ecs_system_flicker);
    if (flicker_system_code != RV_OK) {
        return flicker_system_code;
    }
    return RV_OK;
}

// Loads the scene into world_, then fills keys_ and the board from its entities and schedules the lamps. An error
// leaves them partly filled; the caller clears them.
int rv_dmain::read_scene()
{
    const int register_code = register_ecs();
    if (register_code != RV_OK) {
        return register_code;
    }
    rv_cd *cd = rv_pdko_cd(pdk_);
    uint32_t load_number = 0;
    const int load_code = rv_pdkkit::ecs_scene_load(world_, cd, RV_EXAMPLE_CPP_SCENE_NAME, rv_pdkkit::ecs_id{}, load_number);
    if (load_code != RV_OK) {
        return load_code;
    }
    const int transform_code = rv_pdkkit::ecs_system_transform(world_);
    if (transform_code != RV_OK) {
        return transform_code;
    }
    const int route_code = rv_route_collect(world_, keys_);
    if (route_code != RV_OK) {
        return route_code;
    }
    const int parts_code = collect_parts();
    if (parts_code != RV_OK) {
        return parts_code;
    }
    const int lamp_code = rv_lamps_schedule(world_, keys_);
    if (lamp_code != RV_OK) {
        return lamp_code;
    }
    return RV_OK;
}

// The board from the scene's entities, in file order.
int rv_dmain::collect_parts()
{
    for (const rv_pdkkit::ecs_load &load : world_.loads) {
        for (const rv_pdkkit::ecs_id id : load.made) {
            const auto *name = rv_pdkkit::ecs_component_get<rv_pdkkit::ecs_component_name>(world_, id);
            if (name == nullptr) {
                RV_LOG_ERR("scene", "scene refused: an entity has no name");
                return RV_ERR_INVAL;
            }
            if (name->text == RV_EXAMPLE_CPP_BOARD_NAME) {
                board_found_ = true;
            }
        }
    }
    return RV_OK;
}

void rv_dmain::draw_world()
{
    // No keys means no route: read_scene has already logged why, so nothing is drawn.
    if (keys_.empty()) {
        return;
    }
    const int frame_code = rv_pdkkit::ecs_world_frame(world_, rv_pdkkit::ecs_frame{ pdk_, frame_count_ });
    if (frame_code != RV_OK) {
        log_draw_error("world", frame_code);
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
    const int64_t sound_code = rv_sound_init(pdk_, keys_, sound_state_);
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
    const int64_t sound_code = rv_sound_update(pdk_, frame_count_ % RV_ROUTE_PERIOD_FRAMES, board_found_, sound_state_);
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
