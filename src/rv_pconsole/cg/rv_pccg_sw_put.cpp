#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "pdk/cv/rv_primitives.h"
#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_camera/rv_camera.hpp"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_project.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_math.hpp"
#include "pdklib/rv_math/rv_xform.hpp"
#include "rv_pconsole/cg/rv_pccg_sw.hpp"

namespace rv_3dmppc
{

namespace
{

// Per put: the viewport, the depth range, the cull mode and the tint.
struct rv_pccg_sw_setup {
    rv_pdklib::rv_gte_viewport viewport;
    rv_pdklib::rv_gte_depth_range depth;
    uint32_t cull;
    rv_color tint;
};

struct rv_pccg_sw_material {
    const rv_pdklib::rv_mppcmod_part &part;
    const rv_pccg_part_texture &texture;
};

// Triangles not drawn in one put: a uv span that does not fit rv_uv, a vertex outside int32.
struct rv_pccg_sw_skipped {
    int64_t texel = 0;
    int64_t vertex = 0;
};

// A vertex colour, a part colour and a tint each scale 0..255: divide the product by 255 * 255.
constexpr int64_t RV_PCCG_SW_COLOUR_SCALE = 255 * 255;

// rv_uv holds unsigned texels.
constexpr int64_t RV_PCCG_SW_TEXEL_MAX = 65535;

rv_pdklib::rv_vec3 to_vec3(const rv_cg_vec3 &value)
{
    return { value.x, value.y, value.z };
}

// The bottom row of an affine world is 0 0 0 1; rv_gte_matrix keeps no bottom row to carry the rest.
bool world_affine(const float (&world)[16])
{
    return world[12] == 0.0f && world[13] == 0.0f && world[14] == 0.0f && world[15] == 1.0f;
}

// RV_CG_CULL_BACK drops the faces turned away from the eye. A front face is counter-clockwise on
// screen (y points down), and gte_cull_screen_cw keeps exactly those, as in rv_gte_culled.
uint32_t gte_cull_mode(uint32_t cull)
{
    return cull == RV_CG_CULL_NONE ? rv_pdklib::gte_cull_none : rv_pdklib::gte_cull_screen_cw;
}

// The vertex value is 0..255 and the product of three at most 255^3, so the result is 0..255.
uint8_t scale_channel(int32_t vertex, uint8_t part, uint8_t tint)
{
    const int64_t product = static_cast<int64_t>(vertex) * part * tint;
    return static_cast<uint8_t>(rv_pdklib::rv_gte_div_round(product, RV_PCCG_SW_COLOUR_SCALE));
}

// Floor of n / d for d > 0: integer division rounds toward zero, the repeat shift needs the floor.
int64_t floor_div(int64_t n, int64_t d)
{
    const int64_t q = n / d;
    return n % d < 0 ? q - 1 : q;
}

// Texel coordinates of one triangle, uv * size in 1/4096 texel. TILE repeats the texture, so whole
// periods move every vertex so the smallest coordinate is in [0, period). A yes or no: no means the
// triangle does not fit rv_uv and is not drawn.
bool texel_uvs(const rv_pccg_sw_clip_vertex (&tri)[RV_PCCG_SW_TRI_CORNERS],
    const rv_pccg_part_texture &texture,
    rv_uv (&out)[RV_PCCG_SW_TRI_CORNERS])
{
    const int64_t period_u = texture.width << rv_pdklib::gte_frac_bits;
    const int64_t period_v = texture.height << rv_pdklib::gte_frac_bits;
    int64_t u[RV_PCCG_SW_TRI_CORNERS] = {};
    int64_t v[RV_PCCG_SW_TRI_CORNERS] = {};
    for (int k = 0; k < RV_PCCG_SW_TRI_CORNERS; ++k) {
        u[k] = tri[k].uv[0] * texture.width;
        v[k] = tri[k].uv[1] * texture.height;
    }
    const int64_t shift_u = -floor_div(std::min({ u[0], u[1], u[2] }), period_u) * period_u;
    const int64_t shift_v = -floor_div(std::min({ v[0], v[1], v[2] }), period_v) * period_v;
    for (int k = 0; k < RV_PCCG_SW_TRI_CORNERS; ++k) {
        const int64_t tu = (u[k] + shift_u) >> rv_pdklib::gte_frac_bits;
        const int64_t tv = (v[k] + shift_v) >> rv_pdklib::gte_frac_bits;
        if (tu > RV_PCCG_SW_TEXEL_MAX || tv > RV_PCCG_SW_TEXEL_MAX) {
            return false;
        }
        out[k] = rv_uv{ static_cast<uint16_t>(tu), static_cast<uint16_t>(tv) };
    }
    return true;
}

// One cut triangle as a polygon primitive: screen, zero area, cull, depth key, fill. `drawn` is false
// if it draws nothing; a zero-area triangle is not counted against the budget either.
// ponytail: a vertex far off the screen saturates in int16 (rv_vertex) and distorts the triangle; kept,
// so the floor next to the camera stays. Upgrade: clip against the screen before the vertex is made.
int make_primitive(const rv_pccg_sw_setup &setup,
    const rv_pccg_sw_clip_vertex (&tri)[RV_PCCG_SW_TRI_CORNERS],
    const rv_pccg_sw_material &material,
    rv_primitive &out,
    bool &drawn,
    rv_pccg_sw_skipped &skipped)
{
    drawn = false;
    rv_pdklib::rv_gte_screen screen[RV_PCCG_SW_TRI_CORNERS] = {};
    for (int k = 0; k < RV_PCCG_SW_TRI_CORNERS; ++k) {
        const int rc = rv_pdklib::rv_gte_project(tri[k].view, setup.viewport, screen[k]);
        if (rc != RV_OK) {
            return rc;
        }
    }
    // Zero area is culled in both modes and the two differ only in sign: both true means zero area.
    const bool zero_area = rv_pdklib::rv_gte_culled(screen[0], screen[1], screen[2], rv_pdklib::gte_cull_screen_cw) &&
        rv_pdklib::rv_gte_culled(screen[0], screen[1], screen[2], rv_pdklib::gte_cull_screen_ccw);
    if (zero_area || rv_pdklib::rv_gte_culled(screen[0], screen[1], screen[2], setup.cull)) {
        return RV_OK;
    }

    const bool textured = material.texture.addr_texture != 0;
    rv_uv texel[RV_PCCG_SW_TRI_CORNERS] = {};
    if (textured && !texel_uvs(tri, material.texture, texel)) {
        ++skipped.texel;
        return RV_OK;
    }
    const rv_pdklib::fx depth_z[RV_PCCG_SW_TRI_CORNERS] = { tri[0].view.z, tri[1].view.z, tri[2].view.z };
    int32_t key = 0;
    const int rc = rv_pdklib::rv_gte_depth_key(depth_z, RV_PCCG_SW_TRI_CORNERS, setup.depth, key);
    if (rc != RV_OK) {
        return rc;
    }

    out = rv_primitive{};
    out.type = RV_PRIMITIVE_POLYGON;
    out.depth = key;
    rv_polygon &polygon = out.data.polygon;
    polygon.fill_mode = textured ? RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE : RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
    polygon.addr_texture = textured ? material.texture.addr_texture : 0;
    polygon.addr_palette = textured ? material.texture.addr_palette : 0;
    polygon.mapping = textured ? RV_TEXWRAP_TILE : RV_TEXWRAP_CLAMP;
    polygon.vertex_count = RV_PCCG_SW_TRI_CORNERS;
    for (int k = 0; k < RV_PCCG_SW_TRI_CORNERS; ++k) {
        const rv_pdklib::rv_gte_screen16 point = rv_pdklib::rv_gte_to_int16(screen[k]);
        const std::array<uint8_t, 3> &part = material.part.color;
        const rv_color colour = { scale_channel(tri[k].colour[0], part[0], setup.tint.r),
            scale_channel(tri[k].colour[1], part[1], setup.tint.g),
            scale_channel(tri[k].colour[2], part[2], setup.tint.b) };
        polygon.vertexes[k] = { point.x, point.y, colour, texel[k] };
    }
    drawn = true;
    return RV_OK;
}

// Every triangle of every part, cut at the near plane and culled, in part order. A triangle with a
// vertex outside int32 is skipped and counted, before the cut.
int build_primitives(const rv_pccg_sw_setup &setup,
    const rv_pdklib::rv_mppcmod_int_model &model,
    const std::vector<rv_pccg_sw_view_vertex> &view,
    std::span<const rv_pccg_part_texture> textures,
    std::vector<rv_primitive> &out,
    rv_pccg_sw_skipped &skipped)
{
    for (size_t p = 0; p < model.parts.size(); ++p) {
        const rv_pccg_sw_material material = { model.parts[p], textures[p] };
        for (uint32_t t = model.parts[p].first_tri; t < model.parts[p].first_tri + model.parts[p].tri_count; ++t) {
            rv_pccg_sw_clip_vertex in[RV_PCCG_SW_TRI_CORNERS];
            bool valid = true;
            for (size_t k = 0; k < RV_PCCG_SW_TRI_CORNERS; ++k) {
                const auto index = model.triangles[t].index[k];
                const rv_pdklib::rv_mppcmod_int_vertex &vertex = model.vertices[index];
                in[k] = { view[index].view,
                    { vertex.color[0], vertex.color[1], vertex.color[2] },
                    { vertex.uv[0], vertex.uv[1] } };
                valid = valid && view[index].valid;
            }
            if (!valid) {
                ++skipped.vertex;
                continue;
            }
            rv_pccg_sw_clip_vertex cut[RV_PCCG_SW_CUT_CORNERS];
            int count = 0;
            const int clip_rc = rv_pccg_sw_clip_near(in, setup.depth.near_plane, cut, count);
            if (clip_rc != RV_OK) {
                return clip_rc;
            }
            // A quad left by the cut is a fan of two triangles from its first vertex.
            for (int k = 1; k + 1 < count; ++k) {
                const rv_pccg_sw_clip_vertex tri[RV_PCCG_SW_TRI_CORNERS] = { cut[0], cut[k], cut[k + 1] };
                rv_primitive primitive{};
                bool drawn = false;
                const int rc = make_primitive(setup, tri, material, primitive, drawn, skipped);
                if (rc != RV_OK) {
                    return rc;
                }
                if (drawn) {
                    out.push_back(primitive);
                }
            }
        }
    }
    return RV_OK;
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
    const bool affine = world_affine(put->world);
    if (!affine) {
        RV_LOG_WARN("pccg", "model {} refused: the world transform is not affine, its bottom row is not 0 0 0 1", model_addr);
        return RV_ERR_INVAL;
    }
    rv_pdklib::rv_gte_matrix world = {};
    const bool world_bad = rv_pccg_sw_world_matrix(put->world, world) != RV_OK;
    if (world_bad) {
        RV_LOG_WARN("pccg", "model {} refused: the world transform leaves rv_gte's range", model_addr);
        return RV_ERR_INVAL;
    }
    std::vector<rv_pccg_sw_view_vertex> view;
    const int view_rc = rv_pccg_sw_view_vertexes(gte_camera_.view, world, it->int_model, view);
    if (view_rc != RV_OK) {
        RV_LOG_WARN("pccg", "model {} refused: the composed matrix leaves rv_gte's range (rc {})", model_addr, view_rc);
        return RV_ERR_INVAL;
    }
    int32_t width = 0;
    int32_t height = 0;
    const bool width_bad = rv_pccg_sw_screen_side(cv_.screen_width(), width) != RV_OK;
    const bool height_bad = rv_pccg_sw_screen_side(cv_.screen_height(), height) != RV_OK;
    if (width_bad || height_bad) {
        RV_LOG_WARN("pccg", "model {} refused: the screen side is outside (0, INT32_MAX]", model_addr);
        return RV_ERR_INVAL;
    }
    const rv_pccg_sw_setup setup = {
        { gte_camera_.h, width / 2, height / 2 },
        { gte_camera_.near_plane, gte_camera_.far_plane, put->depth_min, put->depth_max },
        gte_cull_mode(put->cull),
        put->tint,
    };
    std::vector<rv_primitive> primitives;
    rv_pccg_sw_skipped skipped = {};
    const bool build_bad = build_primitives(setup, it->int_model, view, it->textures, primitives, skipped) != RV_OK;
    if (build_bad) {
        RV_LOG_WARN("pccg", "model {} refused: a triangle leaves rv_gte's range", model_addr);
        return RV_ERR_INVAL;
    }
    // Warned once per put with the count, as texel_skipped: the frame keeps no state for a once-per-frame warning.
    if (skipped.vertex > 0) {
        RV_LOG_WARN("pccg", "model {}: {} triangle(s) not drawn, a vertex is outside int32", model_addr, skipped.vertex);
    }
    if (skipped.texel > 0) {
        RV_LOG_WARN("pccg",
            "model {}: {} triangle(s) not drawn, their uv span is wider than rv_uv's texel range or not a number",
            model_addr,
            skipped.texel);
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

int rv_pccg_sw_fx_vec3(const rv_cg_vec3 &value, rv_pdklib::fx_vec3 &out)
{
    rv_pdklib::fx_vec3 v = {};
    const int rc_x = rv_pdklib::fx_from_double(value.x, v.x);
    const int rc_y = rv_pdklib::fx_from_double(value.y, v.y);
    const int rc_z = rv_pdklib::fx_from_double(value.z, v.z);
    if (rc_x != RV_OK || rc_y != RV_OK || rc_z != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = v;
    return RV_OK;
}

int rv_pccg_sw_screen_side(int64_t side, int32_t &out)
{
    const bool fits = side > 0 && side <= std::numeric_limits<int32_t>::max();
    if (!fits) {
        return RV_ERR_INVAL;
    }
    out = static_cast<int32_t>(side);
    return RV_OK;
}

int rv_pccg_sw_point_int(const rv_pdklib::rv_gte_camera &camera,
    rv_pdklib::rv_gte_viewport viewport,
    rv_pdklib::fx_vec3 world,
    int32_t depth_min,
    int32_t depth_max,
    rv_pdklib::rv_gte_screen &screen,
    int32_t &depth)
{
    const int32_t in[3] = { world.x, world.y, world.z };
    int32_t view_raw[3] = {};
    int rc = rv_pdklib::rv_gte_point(camera.view, in, view_raw);
    if (rc != RV_OK) {
        return rc;
    }
    const rv_pdklib::fx_vec3 view = { view_raw[0], view_raw[1], view_raw[2] };
    if (view.z < camera.near_plane) {
        return RV_ERR_INVAL;
    }
    const rv_pdklib::fx depth_z[1] = { view.z };
    const rv_pdklib::rv_gte_depth_range range = { camera.near_plane, camera.far_plane, depth_min, depth_max };
    int32_t key = 0;
    rc = rv_pdklib::rv_gte_depth_key(depth_z, 1, range, key);
    if (rc != RV_OK) {
        return rc;
    }
    rv_pdklib::rv_gte_screen point = {};
    rc = rv_pdklib::rv_gte_project(view, viewport, point);
    if (rc != RV_OK) {
        return rc;
    }
    screen = point;
    depth = key;
    return RV_OK;
}

int64_t rv_pccg_sw::point(const rv_cg_vec3 *world, int32_t depth_min, int32_t depth_max, rv_cg_screen *out)
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
    const bool depth_ordered = depth_min <= depth_max;
    if (!depth_ordered) {
        RV_LOG_WARN("pccg", "point refused: depth_min {} is above depth_max {}", depth_min, depth_max);
        return RV_ERR_INVAL;
    }
    rv_pdklib::fx_vec3 world_fx = {};
    const bool world_bad = rv_pccg_sw_fx_vec3(*world, world_fx) != RV_OK;
    if (world_bad) {
        RV_LOG_WARN("pccg", "point refused: a world value leaves Q20.12");
        return RV_ERR_INVAL;
    }
    // transitional until E11e-2: the screen leaves as float, the rv_cg contract
    int32_t width = 0;
    int32_t height = 0;
    const bool width_bad = rv_pccg_sw_screen_side(cv_.screen_width(), width) != RV_OK;
    const bool height_bad = rv_pccg_sw_screen_side(cv_.screen_height(), height) != RV_OK;
    if (width_bad || height_bad) {
        RV_LOG_WARN("pccg", "point refused: the screen side is outside (0, INT32_MAX]");
        return RV_ERR_INVAL;
    }
    // the centre: rv_xform_to_screen maps ndc 0 to half the side
    const rv_pdklib::rv_gte_viewport viewport = { gte_camera_.h, width / 2, height / 2 };
    rv_pdklib::rv_gte_screen screen = {};
    int32_t depth = 0;
    // A point nearer than the near plane is an ordinary answer: the HUD asks every frame.
    if (rv_pccg_sw_point_int(gte_camera_, viewport, world_fx, depth_min, depth_max, screen, depth) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out->x = static_cast<float>(screen.x);
    out->y = static_cast<float>(screen.y);
    out->depth = depth;
    return RV_OK;
}

} // namespace rv_3dmppc
