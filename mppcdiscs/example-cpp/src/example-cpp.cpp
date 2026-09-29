#include <cstdint>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/cio/rv_cio.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/de/rv_de.h"
#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_camera/rv_camera.hpp"
#include "pdklib/rv_cppdisc/rv_cppdisc.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_xform.hpp"
#include "pdklib/rv_scene/rv_scene.hpp"
#include "pdklib/rv_scene/rv_scene_disc.hpp"
// The scene loader is compiled into this disc here, once (see the header).
#include "pdklib/rv_scene/rv_scene_unit.hpp"

namespace example_cpp
{

namespace
{

constexpr int32_t RV_EXAMPLE_CPP_DEPTH_SPRITE = 0;
constexpr int32_t RV_EXAMPLE_CPP_DEPTH_BAR = 400;
// The scene's boxes sort among themselves between the sprites and the bar.
constexpr int32_t RV_EXAMPLE_CPP_DEPTH_SCENE_FAR = 100;
constexpr int32_t RV_EXAMPLE_CPP_DEPTH_SCENE_NEAR = 350;

// The scene this disc draws, as scenes/main.scene.toml lands on the disc.
constexpr const char *RV_EXAMPLE_CPP_SCENE_NAME = "main.scene.toml";

// A unit box, one face per row: four corners in the PDK's Z order (top-left,
// top-right, bottom-left, bottom-right) as the face is seen from outside, and
// its colour. Seen from inside, a face winds the other way and is culled.
struct rv_example_cpp_face {
    rv_pdklib::rv_vec3 corner[4];
    rv_color color;
};
constexpr float h = 0.5f;
constexpr rv_example_cpp_face RV_EXAMPLE_CPP_BOX[6] = {
    { { { h, h, h }, { -h, h, h }, { h, -h, h }, { -h, -h, h } }, rv_color{ 200, 90, 70 } },
    { { { -h, h, -h }, { h, h, -h }, { -h, -h, -h }, { h, -h, -h } }, rv_color{ 220, 150, 80 } },
    { { { h, h, -h }, { h, h, h }, { h, -h, -h }, { h, -h, h } }, rv_color{ 90, 170, 110 } },
    { { { -h, h, h }, { -h, h, -h }, { -h, -h, h }, { -h, -h, -h } }, rv_color{ 70, 120, 200 } },
    { { { -h, h, h }, { h, h, h }, { -h, h, -h }, { h, h, -h } }, rv_color{ 230, 220, 120 } },
    { { { h, -h, h }, { -h, -h, h }, { h, -h, -h }, { -h, -h, -h } }, rv_color{ 110, 90, 150 } },
};

// The one texture this disc draws. Never acquired or released, only ever
// named - the drive makes it resident the first time frame_render asks for
// this name, and frees it itself when this disc unloads.
constexpr const char *RV_EXAMPLE_CPP_TEXTURE_NAME = "example-sprite.mppctex";

} // namespace

// rv_dmain_base_ carries everything pdklib/rv_cppdisc/rv_cppdisc.hpp
// considers the same for any C++ disc: the startup guards, MENU-button
// press-edge tracking and read_asset(). rv_dmain adds exactly what makes
// this disc THIS game - the sprite grid, the text bar and their layout.
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

        std::vector<uint8_t> text;
        if (read_asset("example-text.txt", text)) {
            text_bytes_ = static_cast<int64_t>(text.size());
        }

        // No scene, or one that does not read: the disc runs without it and says why.
        std::string error;
        if (rv_pdklib::rv_scene_load(rv_pdko_cd(pdk_), RV_EXAMPLE_CPP_SCENE_NAME, scene_, error) != 0) {
            RV_LOG_ERR("scene", "{}", error);
        }

        return RV_OK;
    }

    void frame_update(float dt)
    {
        rv_dmain_base_::frame_update(dt);
        if (dt > 0.0f) {
            phase_ += dt;
        }
    }

    void frame_render();
    void draw_scene();

    void disc_shutdown()
    {
        // Nothing to release: this disc never acquired the texture it drew, only
        // named it, and the drive frees everything it made resident on its own
        // when this disc unloads.
    }

    const char *disc_title() const
    {
        return "example-cpp";
    }

private:
    int64_t text_bytes_ = 0;
    float phase_ = 0.0f;
    rv_pdklib::rv_scene scene_;
};

// Each mesh object a box, seen through the scene's first camera (its +z looks ahead).
void rv_dmain::draw_scene()
{
    using namespace rv_pdklib;
    rv_vec3 eye{ 0.0f, 0.8f, -4.0f };
    rv_vec3 forward{ 0.0f, 0.0f, 1.0f };
    for (std::size_t i = 0; i < scene_.objects.size(); ++i) {
        if (scene_.objects[i].kind == "camera") {
            const rv_mat4 m = rv_scene_world(scene_, i);
            eye = rv_mat4_mul_point(m, rv_vec3{ 0.0f, 0.0f, 0.0f });
            forward = rv_mat4_mul_direction(m, rv_vec3{ 0.0f, 0.0f, 1.0f });
            break;
        }
    }
    const float w = static_cast<float>(screen_width_);
    const float hgt = static_cast<float>(screen_height_);
    const rv_camera camera = rv_camera_make(eye, eye + forward, rv_vec3{ 0.0f, 1.0f, 0.0f }, 60.0f * 3.14159265f / 180.0f,
        w / hgt, 0.1f, 30.0f);
    for (std::size_t i = 0; i < scene_.objects.size(); ++i) {
        if (scene_.objects[i].kind != "mesh") {
            continue;
        }
        const rv_xform_conf conf = rv_xform_conf_make(rv_camera_mvp(camera, rv_scene_world(scene_, i)), camera, w, hgt,
            RV_EXAMPLE_CPP_DEPTH_SCENE_FAR, RV_EXAMPLE_CPP_DEPTH_SCENE_NEAR, RV_CULL_SCREEN_CCW);
        for (const rv_example_cpp_face &face : RV_EXAMPLE_CPP_BOX) {
            rv_xform_vertex v[4];
            for (int k = 0; k < 4; ++k) {
                v[k] = rv_xform_vertex{ face.corner[k], face.color, rv_uv{} };
            }
            rv_primitive primitive;
            if (rv_xform_quad(conf, v, primitive)) {
                rv_cv_frame_put(rv_pdko_cv(pdk_), &primitive);
            }
        }
    }
}

void rv_dmain::frame_render()
{
    frame_begin(rv_color{ 20, 24, 40 });
    draw_scene();

    int64_t addr_texture = 0;
    int64_t addr_palette = 0;
    if (texture_resident(RV_EXAMPLE_CPP_TEXTURE_NAME, addr_texture, addr_palette)) {
        const rv_texture_mapping_type modes[3] = {
            RV_TEXWRAP_CLAMP, RV_TEXWRAP_TILE, RV_TEXWRAP_STRETCH
        };
        const float size = 48.0f;
        const float gap = 12.0f;
        const float total = 3.0f * size + 2.0f * gap;
        const float x0 = (static_cast<float>(screen_width_) - total) * 0.5f;
        const float y0 = (static_cast<float>(screen_height_) - size) * 0.5f;

        for (int i = 0; i < 3; ++i) {
            rv_sprite sprite = {};
            sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE;
            sprite.addr_texture = addr_texture;
            sprite.addr_palette = addr_palette;
            sprite.color = rv_color{ 255, 255, 255 };
            sprite.mapping = modes[i];
            sprite.x = static_cast<int16_t>(x0 + static_cast<float>(i) * (size + gap));
            sprite.y = static_cast<int16_t>(y0);
            sprite.width = static_cast<uint16_t>(size);
            sprite.height = static_cast<uint16_t>(size);

            draw_sprite(sprite, RV_EXAMPLE_CPP_DEPTH_SPRITE);
        }
    }

    if (text_bytes_ > 0) {
        rv_sprite sprite = {};
        sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        sprite.addr_texture = 0;
        sprite.addr_palette = 0;
        sprite.color = rv_color{ 90, 210, 220 };
        sprite.mapping = RV_TEXWRAP_CLAMP;
        sprite.x = 8;
        sprite.y = static_cast<int16_t>(screen_height_ - 16);
        sprite.width = static_cast<uint16_t>(text_bytes_ * 4);
        sprite.height = 8;

        draw_sprite(sprite, RV_EXAMPLE_CPP_DEPTH_BAR);
    }

    frame_end();
}

} // namespace example_cpp

RV_MPPC_DISC_ENTRY_DEF(example_cpp::rv_dmain);
