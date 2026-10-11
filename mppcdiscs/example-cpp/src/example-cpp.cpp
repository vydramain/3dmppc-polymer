#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_cppdisc/rv_cppdisc.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "example-cpp-board.hpp"
#include "example-cpp-draw.hpp"
#include "example-cpp-lamps.hpp"
#include "example-cpp-route.hpp"
#include "example-cpp-sound.hpp"

namespace example_cpp
{

namespace
{

// The scene this disc draws, as scenes/main.scene.toml lands on the disc.
constexpr const char *RV_EXAMPLE_CPP_SCENE_NAME = "main.scene.toml";
// The scene object that carries the factory board lettering.
constexpr const char *RV_EXAMPLE_CPP_BOARD_NAME = "board";

constexpr rv_color RV_EXAMPLE_CPP_BACKGROUND = { 20, 24, 40 };

// A mesh of the scene, ready to put: its model name and the put without the model address.
struct rv_example_cpp_mesh {
    std::string model; // .mppcmod name on the disc
    rv_cg_put put;
};

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
            meshes_.clear();
            keys_.clear();
            lamps_.clear();
            RV_LOG_ERR("scene", "{} not used: rv_err {}", RV_EXAMPLE_CPP_SCENE_NAME, read_code);
        }
        if (read_code == RV_OK && !board_found_) {
            RV_LOG_WARN("scene", "no '{}' object: the board has no lettering", RV_EXAMPLE_CPP_BOARD_NAME);
        }
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
        return "example-cpp";
    }

private:
    int read_scene();
    void add_mesh(const rv_cd_scene_object &object);
    void add_route_key(const rv_cd_scene_object &object);
    void draw_world();
    void log_draw_error(const char *step, int64_t code);
    void play_sound();
    void log_sound_error(int64_t code);

    std::vector<rv_example_cpp_mesh> meshes_;
    // The cam_NN keys, in NN order.
    std::vector<rv_route_key> keys_;
    // The lamp_N objects, each with the frames it burns; filled after keys_ is sorted.
    std::vector<rv_lamp> lamps_;
    // World matrix of the board object, valid when board_found_.
    float board_world_[16] = {};
    bool board_found_ = false;
    // Frames updated while running: the demo's clock.
    int64_t frame_count_ = 0;
    bool draw_error_logged_ = false;
    // The music and the clack: the last frame played for and the area of its music.
    rv_sound_state sound_state_;
    bool sound_error_logged_ = false;
};

// Fills meshes_ and keys_ from the scene. An error leaves them partly filled; the caller clears them.
int rv_dmain::read_scene()
{
    rv_cd *cd = rv_pdko_cd(pdk_);
    const int64_t count = rv_cd_scene_object_count(cd, RV_EXAMPLE_CPP_SCENE_NAME);
    if (count < 0) {
        return static_cast<int>(count);
    }
    for (int64_t i = 0; i < count; ++i) {
        rv_cd_scene_object object = {};
        const int64_t read_code = rv_cd_scene_object_read(cd, RV_EXAMPLE_CPP_SCENE_NAME, i, &object);
        if (read_code < 0) {
            return static_cast<int>(read_code);
        }
        if (std::strcmp(object.name, RV_EXAMPLE_CPP_BOARD_NAME) == 0) {
            board_found_ = true;
            std::memcpy(board_world_, object.world, sizeof(board_world_));
        }
        if (object.kind == RV_CD_SCENE_MESH) {
            add_mesh(object);
        } else if (object.kind == RV_CD_SCENE_CAMERA) {
            add_route_key(object);
        }
        if (object.kind == RV_CD_SCENE_MESH && rv_lamp_name(object.name)) {
            rv_lamp lamp = {};
            lamp.name = object.name;
            std::memcpy(lamp.world, object.world, sizeof(lamp.world));
            lamps_.push_back(lamp);
        }
    }
    if (keys_.empty()) {
        return RV_ERR_NOENT;
    }
    std::sort(keys_.begin(), keys_.end(), [](const rv_route_key &a, const rv_route_key &b) {
        return a.index < b.index;
    });
    rv_lamps_schedule(keys_, lamps_);
    return RV_OK;
}

void rv_dmain::add_mesh(const rv_cd_scene_object &object)
{
    if (object.model[0] == '\0') {
        RV_LOG_WARN("scene", "mesh '{}' has no model, not drawn", object.name);
        return;
    }
    rv_example_cpp_mesh mesh = {};
    mesh.model = object.model;
    std::memcpy(mesh.put.world, object.world, sizeof(mesh.put.world));
    mesh.put.depth_min = RV_DRAW_DEPTH_WORLD_MIN;
    mesh.put.depth_max = RV_DRAW_DEPTH_WORLD_MAX;
    mesh.put.cull = RV_CG_CULL_BACK;
    mesh.put.tint = object.tint;
    meshes_.push_back(mesh);
}

// The camera object's eye is world * (0,0,0) and its view direction world * (0,0,1)
// minus the eye: the translation column and the z column of the affine world.
void rv_dmain::add_route_key(const rv_cd_scene_object &object)
{
    int32_t index = 0;
    if (!rv_route_key_name(object.name, index)) {
        return; // a camera outside the route is not part of the loop
    }
    rv_route_key key = {};
    key.index = index;
    key.eye = { object.world[3], object.world[7], object.world[11] };
    key.dir = rv_route_unit({ object.world[2], object.world[6], object.world[10] });
    keys_.push_back(key);
}

void rv_dmain::draw_world()
{
    // No keys means no camera; read_scene has already logged why, so nothing is drawn.
    if (keys_.empty()) {
        return;
    }
    const rv_cg_camera camera = rv_route_camera_at(keys_, frame_count_);
    const int64_t camera_code = rv_cg_frame_camera(rv_pdko_cg(pdk_), &camera);
    if (camera_code < 0) {
        log_draw_error("camera", camera_code);
        return;
    }
    for (rv_example_cpp_mesh &mesh : meshes_) {
        const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk_), RV_CD_RESOURCE_MODEL, mesh.model.c_str());
        const int64_t put_code = (addr < 0) ? addr : rv_cg_model_put(rv_pdko_cg(pdk_), addr, &mesh.put);
        if (put_code < 0) {
            log_draw_error(mesh.model.c_str(), put_code);
        }
    }
    const int64_t lamps_code = rv_lamps_draw(pdk_, lamps_, frame_count_ % RV_ROUTE_PERIOD_FRAMES);
    if (lamps_code < 0) {
        log_draw_error("lamps", lamps_code);
    }
    if (board_found_) {
        const int64_t board_code = rv_board_draw(pdk_, board_world_, frame_count_ % RV_ROUTE_PERIOD_FRAMES);
        if (board_code < 0) {
            log_draw_error("board", board_code);
        }
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

// Plays the frame's sound; no keys means no camera and so no area, and read_scene has logged why.
void rv_dmain::play_sound()
{
    if (keys_.empty()) {
        return;
    }
    const int64_t sound_code = rv_sound_update(pdk_, keys_, frame_count_ % RV_ROUTE_PERIOD_FRAMES, board_found_, sound_state_);
    if (sound_code < 0) {
        log_sound_error(sound_code);
    }
}

// Logs the first failure only, as log_draw_error does: the same failure would repeat in every frame.
void rv_dmain::log_sound_error(int64_t code)
{
    if (sound_error_logged_) {
        return;
    }
    sound_error_logged_ = true;
    RV_LOG_ERR("sound", "sound not played: rv_err {}", code);
}

} // namespace example_cpp

RV_MPPC_DISC_ENTRY_DEF(example_cpp::rv_dmain);
