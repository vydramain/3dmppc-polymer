#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include "pdk/cv/rv_primitives.h"
#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_camera/rv_camera.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_math.hpp"
#include "pdklib/rv_math/rv_xform.hpp"
#include "rv_pconsole/cg/rv_pccg_sw.hpp"

namespace rv_3dmppc
{

namespace
{

// A vertex in the clip space of one put. The near cut interpolates the colour (0..255 per
// channel, before the tint) and the glTF uv along with the position.
struct rv_pccg_sw_clip_vertex {
    rv_pdklib::rv_vec4 clip;
    rv_pdklib::rv_vec3 colour;
    rv_pdklib::rv_vec2 uv;
};

// A vertex colour, a part colour and a tint each scale 0..255, so the product of three is
// divided by 255 * 255 to come back to 0..255.
constexpr float RV_PCCG_SW_COLOUR_SCALE = 255.0f * 255.0f;
constexpr float RV_PCCG_SW_CHANNEL_MAX = 255.0f;

// rv_uv holds unsigned texels.
constexpr float RV_PCCG_SW_TEXEL_MAX = 65535.0f;

// The depth window for point(): a point has no put and so no range of its own. The manifest
// does not configure the ordering-table window (rv_pboot_conf leaves it), so the rv_pccv_conf
// defaults are the window the console runs with; rv_pccv does not expose it to cg.
constexpr int32_t RV_PCCG_SW_POINT_DEPTH_MIN = rv_pccv_conf{}.depth_min;
constexpr int32_t RV_PCCG_SW_POINT_DEPTH_MAX = rv_pccv_conf{}.depth_max;

rv_pdklib::rv_vec3 to_vec3(const rv_cg_vec3 &value)
{
    return { value.x, value.y, value.z };
}

rv_pdklib::rv_mat4 world_matrix(const float (&world)[16])
{
    rv_pdklib::rv_mat4 matrix{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            matrix.m[row][col] = world[row * 4 + col];
        }
    }
    return matrix;
}

// RV_CG_CULL_BACK drops the faces turned away from the eye. A front face stays counter-clockwise
// on screen (y points down, so the pixel area sign flips in rv_xform_culled), and RV_CULL_SCREEN_CW
// keeps exactly those.
uint32_t cull_mode(uint32_t cull)
{
    return cull == RV_CG_CULL_NONE ? rv_pdklib::RV_CULL_NONE : rv_pdklib::RV_CULL_SCREEN_CW;
}

rv_pccg_sw_clip_vertex clip_vertex_make(const rv_pdklib::rv_mat4 &mvp, const rv_pdklib::rv_mppcmod_vertex &vertex)
{
    const rv_pdklib::rv_vec3 position = { vertex.pos[0], vertex.pos[1], vertex.pos[2] };
    const rv_pdklib::rv_vec3 colour = { static_cast<float>(vertex.color[0]),
        static_cast<float>(vertex.color[1]),
        static_cast<float>(vertex.color[2]) };
    return { rv_pdklib::rv_world_to_clip(mvp, position), colour, { vertex.uv[0], vertex.uv[1] } };
}

rv_pccg_sw_clip_vertex lerp_vertex(const rv_pccg_sw_clip_vertex &a, const rv_pccg_sw_clip_vertex &b, float t)
{
    return { a.clip + (b.clip - a.clip) * t, a.colour + (b.colour - a.colour) * t, a.uv + (b.uv - a.uv) * t };
}

// Sutherland-Hodgman against the plane w == near. Clip space has w == view z, the same
// quantity rv_xform_near_reject tests; the cut lands exactly on w == near, so the divide
// of a cut vertex is finite. Returns 0, 3 or 4 vertices in `out`.
int clip_near(const rv_pccg_sw_clip_vertex (&in)[3], float near_plane, rv_pccg_sw_clip_vertex (&out)[4])
{
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        const rv_pccg_sw_clip_vertex &a = in[i];
        const rv_pccg_sw_clip_vertex &b = in[(i + 1) % 3];
        const float da = a.clip.w - near_plane;
        const float db = b.clip.w - near_plane;
        if (da >= 0.0f) {
            out[count++] = a;
        }
        if ((da >= 0.0f) != (db >= 0.0f)) {
            out[count++] = lerp_vertex(a, b, da / (da - db));
        }
    }
    return count;
}

uint8_t scale_channel(float vertex, uint8_t part, uint8_t tint)
{
    const float value = vertex * static_cast<float>(part) * static_cast<float>(tint) / RV_PCCG_SW_COLOUR_SCALE;
    return static_cast<uint8_t>(std::clamp(std::floor(value + 0.5f), 0.0f, RV_PCCG_SW_CHANNEL_MAX));
}

rv_color tint_colour(const rv_pdklib::rv_vec3 &vertex, const std::array<uint8_t, 3> &part, const rv_color &tint)
{
    return { scale_channel(vertex.x, part[0], tint.r),
        scale_channel(vertex.y, part[1], tint.g),
        scale_channel(vertex.z, part[2], tint.b) };
}

// NaN and values <= 0 become 0; the screen path saturates to -32768 instead.
uint16_t to_texel(float value)
{
    if (!(value > 0.0f)) {
        return 0;
    }
    return static_cast<uint16_t>(std::min(std::floor(value + 0.5f), RV_PCCG_SW_TEXEL_MAX));
}

// False for NaN and for anything that rounds past the largest texel rv_uv holds.
bool texel_fits(float value)
{
    return value < RV_PCCG_SW_TEXEL_MAX + 0.5f;
}

// Texel coordinates of one triangle. TILE repeats the texture, so moving every vertex by
// whole periods keeps the picture; the shift puts the smallest coordinate in [0, period).
// False if a coordinate still does not fit rv_uv: the triangle is then not drawn.
bool texel_uvs(const rv_pccg_sw_clip_vertex (&tri)[3], const rv_pccg_part_texture &texture, rv_uv (&out)[3])
{
    const float period_u = static_cast<float>(texture.width);
    const float period_v = static_cast<float>(texture.height);
    rv_pdklib::rv_vec2 texel[3];
    for (int k = 0; k < 3; ++k) {
        texel[k] = { tri[k].uv.x * period_u, tri[k].uv.y * period_v };
    }
    const float min_u = std::min({ texel[0].x, texel[1].x, texel[2].x });
    const float min_v = std::min({ texel[0].y, texel[1].y, texel[2].y });
    const float shift_u = -std::floor(min_u / period_u) * period_u;
    const float shift_v = -std::floor(min_v / period_v) * period_v;
    for (int k = 0; k < 3; ++k) {
        const float u = texel[k].x + shift_u;
        const float v = texel[k].y + shift_v;
        if (!texel_fits(u) || !texel_fits(v)) {
            return false;
        }
        out[k] = rv_uv{ to_texel(u), to_texel(v) };
    }
    return true;
}

// The part being drawn: its colour, its texture and the put's tint, passed as one value.
struct rv_pccg_sw_material {
    const rv_pdklib::rv_mppcmod_part &part;
    const rv_pccg_part_texture &texture;
    const rv_color &tint;
};

// One cut triangle as a polygon primitive: screen, zero area, cull, depth key, fill. False if it
// draws nothing; a zero-area triangle is not counted against the budget either.
// ponytail: screen coordinates are int16 (rv_vertex), so a vertex far off the screen saturates
// there and the triangle is distorted. A triangle cut at the near plane can have such a vertex;
// it is kept, because dropping it would remove the floor next to the camera. Upgrade: clip
// against the screen rectangle before the vertex is made.
bool make_primitive(const rv_pdklib::rv_xform_conf &conf,
    const rv_pccg_sw_clip_vertex (&tri)[3],
    const rv_pccg_sw_material &material,
    rv_primitive &out,
    int64_t &texel_skipped)
{
    const rv_pdklib::rv_mppcmod_part &part = material.part;
    const rv_pccg_part_texture &texture = material.texture;
    const rv_color &tint = material.tint;
    const rv_pdklib::rv_vec4 clip[3] = { tri[0].clip, tri[1].clip, tri[2].clip };
    rv_pdklib::rv_vec2 screen[3];
    for (int k = 0; k < 3; ++k) {
        screen[k] = rv_pdklib::rv_xform_to_screen(rv_pdklib::rv_xform_divide(clip[k]), conf.screen_width, conf.screen_height);
    }
    if (rv_pdklib::rv_xform_signed_area2(screen[0], screen[1], screen[2]) == 0.0f) {
        return false;
    }
    if (rv_pdklib::rv_xform_culled(screen[0], screen[1], screen[2], conf.cull)) {
        return false;
    }

    const bool textured = texture.addr_texture != 0;
    rv_uv texel[3] = {};
    if (textured && !texel_uvs(tri, texture, texel)) {
        ++texel_skipped;
        return false;
    }
    out = rv_primitive{};
    out.type = RV_PRIMITIVE_POLYGON;
    out.depth = rv_pdklib::rv_xform_depth_key(clip, 3, conf);

    rv_polygon &polygon = out.data.polygon;
    polygon.fill_mode = textured ? RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE : RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
    polygon.addr_texture = textured ? texture.addr_texture : 0;
    polygon.addr_palette = textured ? texture.addr_palette : 0;
    polygon.mapping = textured ? RV_TEXWRAP_TILE : RV_TEXWRAP_CLAMP;
    polygon.vertex_count = 3;
    for (int k = 0; k < 3; ++k) {
        polygon.vertexes[k] =
            rv_pdklib::rv_xform_vertex_make(screen[k], tint_colour(tri[k].colour, part.color, tint), texel[k]);
    }
    polygon.vertexes[3] = rv_vertex{};
    return true;
}

// Every triangle of every part, cut at the near plane and culled, in part order. Returns the
// triangles skipped because their uv span does not fit rv_uv.
int64_t build_primitives(const rv_pdklib::rv_xform_conf &conf,
    const rv_pdklib::rv_mppcmod &model,
    std::span<const rv_pccg_part_texture> textures,
    const rv_color &tint,
    std::vector<rv_primitive> &out)
{
    int64_t texel_skipped = 0;
    for (size_t p = 0; p < model.parts.size(); ++p) {
        const rv_pdklib::rv_mppcmod_part &part = model.parts[p];
        const rv_pccg_sw_material material = { part, textures[p], tint };
        for (uint32_t t = part.first_tri; t < part.first_tri + part.tri_count; ++t) {
            rv_pccg_sw_clip_vertex in[3];
            for (size_t k = 0; k < 3; ++k) {
                in[k] = clip_vertex_make(conf.mvp, model.vertices[model.triangles[t].index[k]]);
            }
            rv_pccg_sw_clip_vertex cut[4];
            const int count = clip_near(in, conf.near_plane, cut);
            // A quad left by the cut is a fan of two triangles from its first vertex.
            for (int k = 1; k + 1 < count; ++k) {
                const rv_pccg_sw_clip_vertex tri[3] = { cut[0], cut[k], cut[k + 1] };
                rv_primitive primitive{};
                if (make_primitive(conf, tri, material, primitive, texel_skipped)) {
                    out.push_back(primitive);
                }
            }
        }
    }
    return texel_skipped;
}

} // namespace

// --- the frame ---------------------------------------------------------------

rv_pdklib::rv_xform_conf
rv_pccg_sw::frame_conf(const rv_pdklib::rv_mat4 &world, int32_t depth_min, int32_t depth_max, uint32_t cull)
{
    const float width = static_cast<float>(cv_.screen_width());
    const float height = static_cast<float>(cv_.screen_height());
    const rv_pdklib::rv_camera camera = rv_pdklib::rv_camera_make(to_vec3(camera_.eye),
        to_vec3(camera_.target),
        to_vec3(camera_.up),
        camera_.fov_y,
        width / height,
        camera_.near_plane,
        camera_.far_plane);
    return rv_pdklib::rv_xform_conf_make(rv_pdklib::rv_camera_mvp(camera, world),
        camera,
        width,
        height,
        depth_min,
        depth_max,
        cull);
}

int64_t rv_pccg_sw::model_put(int64_t model_addr, const rv_cg_put *put)
{
    const bool no_camera = !camera_set_;
    if (no_camera) {
        RV_LOG_WARN("pccg", "model {} refused: no camera this frame", model_addr);
        return RV_ERR_INVAL;
    }
    const bool no_put = put == nullptr;
    if (no_put) {
        RV_LOG_WARN("pccg", "model {} refused: no put", model_addr);
        return RV_ERR_INVAL;
    }
    const bool cull_known = put->cull == RV_CG_CULL_NONE || put->cull == RV_CG_CULL_BACK;
    if (!cull_known) {
        RV_LOG_WARN("pccg", "model {} refused: cull {} is not a known mode", model_addr, put->cull);
        return RV_ERR_INVAL;
    }
    const bool depth_ordered = put->depth_min <= put->depth_max;
    if (!depth_ordered) {
        RV_LOG_WARN("pccg", "model {} refused: depth_min {} is above depth_max {}", model_addr, put->depth_min, put->depth_max);
        return RV_ERR_INVAL;
    }
    const auto it = find(model_addr);
    const bool unknown_model = it == models_.end();
    if (unknown_model) {
        RV_LOG_WARN("pccg", "model {} refused: no such model", model_addr);
        return RV_ERR_INVAL;
    }
    // A texture is sampled at uv * width, so a textured part needs a size to divide by.
    const bool bad_texture = std::any_of(it->textures.begin(), it->textures.end(), [](const rv_pccg_part_texture &texture) {
        return texture.addr_texture != 0 && (texture.width <= 0 || texture.height <= 0);
    });
    if (bad_texture) {
        RV_LOG_WARN("pccg", "model {} refused: a textured part has no size", model_addr);
        return RV_ERR_INVAL;
    }

    const rv_pdklib::rv_xform_conf conf =
        frame_conf(world_matrix(put->world), put->depth_min, put->depth_max, cull_mode(put->cull));
    std::vector<rv_primitive> primitives;
    const int64_t texel_skipped = build_primitives(conf, it->model, it->textures, put->tint, primitives);
    if (texel_skipped > 0) {
        RV_LOG_WARN("pccg",
            "model {}: {} triangle(s) not drawn, their uv span is wider than rv_uv's texel range or not a number",
            model_addr,
            texel_skipped);
    }

    const int64_t count = static_cast<int64_t>(primitives.size());
    // Compared as the remainder, so the sum can never overflow. The budget is all-or-nothing:
    // nothing is put when the model would not fit.
    const bool over_capacity = count > conf_.triangle_capacity - triangles_;
    if (over_capacity) {
        RV_LOG_WARN("pccg",
            "model {} refused: {} triangle(s) would pass the frame's {}",
            model_addr,
            count,
            conf_.triangle_capacity);
        return RV_ERR_NOMEM;
    }
    // A refusal by cv part-way through leaves the primitives already put: they stay in the frame
    // and count against the budget, the rest are not drawn, and cv's error is returned.
    for (const rv_primitive &primitive : primitives) {
        const int64_t put_rc = cv_.frame_put(&primitive);
        if (put_rc != RV_OK) {
            return put_rc;
        }
        ++triangles_;
    }
    return count;
}

int64_t rv_pccg_sw::point(const rv_cg_vec3 *world, rv_cg_screen *out)
{
    const bool no_point = world == nullptr || out == nullptr;
    if (no_point) {
        RV_LOG_WARN("pccg", "point refused: no world point or no output given");
        return RV_ERR_INVAL;
    }
    const bool no_camera = !camera_set_;
    if (no_camera) {
        RV_LOG_WARN("pccg", "point refused: no camera this frame");
        return RV_ERR_INVAL;
    }
    const rv_pdklib::rv_xform_conf conf = frame_conf(rv_pdklib::rv_mat4_identity(),
        RV_PCCG_SW_POINT_DEPTH_MIN,
        RV_PCCG_SW_POINT_DEPTH_MAX,
        rv_pdklib::RV_CULL_NONE);
    rv_pdklib::rv_vec2 screen{};
    int32_t depth = 0;
    // A point nearer than the near plane is an ordinary answer: the HUD asks every frame.
    if (!rv_pdklib::rv_xform_point(conf, to_vec3(*world), screen, depth)) {
        return RV_ERR_INVAL;
    }
    out->x = screen.x;
    out->y = screen.y;
    out->depth = depth;
    return RV_OK;
}

} // namespace rv_3dmppc
