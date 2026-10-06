// rv_dmain: per-frame drawing of the test grid.
#include "rv_dmain.hpp"

#include "pdk/cv/rv_cv.h"
#include "pdklib/rv_camera/rv_camera.hpp"
#include "pdklib/rv_color/rv_color.hpp"
#include "pdklib/rv_math/rv_math.hpp"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_math/rv_xform.hpp"

#include "rv_dmain_draw_shared.hpp"

namespace rv_service
{

namespace
{

// Ordering-table probes. Larger depth = nearer, so the wrap row sits behind
// the cube and the status bars sit in front of it.
constexpr int32_t RV_DMAIN_DEPTH_ART_LO = -400;
constexpr int32_t RV_DMAIN_DEPTH_ART_HI = 400;
constexpr int32_t RV_DMAIN_DEPTH_TEXT = 900;  // over everything, it explains it

// Polygon vertex counts
constexpr int RV_DMAIN_TRI_VERTICES = 3;
constexpr int RV_DMAIN_QUAD_VERTICES = 4;

// The unit cube: six quads, each in the PDK's Z ORDER, because the console
// splits a quad into triangles (1,2,3) and (2,3,4) rather than walking the rim.
// Wound counter-clockwise as seen from OUTSIDE, which is what
// RV_CULL_SCREEN_CW keeps.
struct rv_dmain_face {
    rv_pdklib::rv_vec3 corner[RV_DMAIN_QUAD_VERTICES];
    rv_pdklib::rv_vec3 normal;
};

constexpr rv_dmain_face RV_DMAIN_CUBE[6] = {
    { { { -1, -1, -1 }, { 1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } }, { 0, 0, -1 } },
    { { { 1, -1, 1 }, { -1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } }, { 0, 0, 1 } },
    { { { 1, -1, -1 }, { 1, -1, 1 }, { 1, 1, -1 }, { 1, 1, 1 } }, { 1, 0, 0 } },
    { { { -1, -1, 1 }, { -1, -1, -1 }, { -1, 1, 1 }, { -1, 1, -1 } }, { -1, 0, 0 } },
    { { { -1, 1, -1 }, { 1, 1, -1 }, { -1, 1, 1 }, { 1, 1, 1 } }, { 0, 1, 0 } },
    { { { -1, -1, 1 }, { 1, -1, 1 }, { -1, -1, -1 }, { 1, -1, -1 } }, { 0, -1, 0 } },
};

using rv_dmain_detail::make_bar;
using rv_dmain_detail::to_screen;

// Screen-space vertex, filled completely - rv_vertex is a plain struct and an
// "almost filled" literal leaves live fields holding whatever the stack had.
rv_vertex make_vertex(int x, int y, rv_color color)
{
    rv_vertex vertex{};
    vertex.x = to_screen(static_cast<float>(x));
    vertex.y = to_screen(static_cast<float>(y));
    vertex.color = color;
    vertex.uv = rv_uv{ 0, 0 }; // read only when fill_mode samples a texture
    return vertex;
}

} // namespace

// --- the test card -----------------------------------------------------------

// Test card. Every cell exercises exactly ONE thing the console can
// draw, and says which. That is what makes it a test rather than a demo: a
// broken feature shows up as one wrong cell with a name on it, instead of a
// picture that is subtly off in a way nobody can localise.
//
// The set below is the console's whole drawing vocabulary, from
// pdk/cv/rv_primitives.h: three primitive kinds, three fill modes, three wrap
// modes, both texel families, the cut-out rule, and the ordering table.
namespace
{

constexpr int RV_DMAIN_GRID_COLS = 4;
constexpr int RV_DMAIN_GRID_ROWS = 3;
constexpr int RV_DMAIN_GRID_TOP = 28;
constexpr int RV_DMAIN_CELL_W = 78;
constexpr int RV_DMAIN_CELL_H = 60;
constexpr int RV_DMAIN_CELL_ART_TOP = 11; // below the cell's label
constexpr int RV_DMAIN_CELL_GRID_OFFSET_X = 4;
constexpr int RV_DMAIN_CELL_ART_OFFSET_X = 3;
constexpr int RV_DMAIN_CELL_LABEL_OFFSET_X = 2;
constexpr int RV_DMAIN_CELL_PADDING_RIGHT = 8;
constexpr int RV_DMAIN_CELL_PADDING_BOTTOM = 5;
constexpr int RV_DMAIN_DEPTH_STEP_OFFSET = 40;
constexpr int RV_DMAIN_DEPTH_LADDER_STEP_X = 12;
constexpr int RV_DMAIN_DEPTH_LADDER_STEP_Y = 8;
constexpr int RV_DMAIN_DEPTH_LADDER_SHRINK_WIDTH = 24;
constexpr int RV_DMAIN_DEPTH_LADDER_SHRINK_HEIGHT = 16;

constexpr const char *RV_DMAIN_CELL_LABEL[] = {
    "LINE",
    "TRI",
    "QUAD",
    "WIRE",
    "SPRITE",
    "DIRECT15",
    "TILE",
    "IDX4",
    "CUTOUT",
    "DEPTH",
    "STRETCH",
    "3D",
};

} // namespace

void rv_dmain::draw_test_grid()
{
    for (int i = 0; i < RV_DMAIN_GRID_COLS * RV_DMAIN_GRID_ROWS; ++i) {
        draw_cell(i);
    }
}

void rv_dmain::draw_cube_cell(int x, int y, int w, int h)
{
    rv_cv *cv = rv_pdko_cv(pdk_);

    // The viewport is the CELL, not the screen: the transform maps normalized
    // coordinates onto the width and height it is handed, anchored at the upper
    // left. A cell-sized pair confines the cube to a box; the offset below then
    // moves that box into place, because the transform offers no offset of its
    // own and the finished vertices are plain int16 screen coordinates.
    const float vw = static_cast<float>(w);
    const float vh = static_cast<float>(h);

    const rv_pdklib::rv_camera camera =
        rv_pdklib::rv_camera_make(rv_pdklib::rv_vec3{ 0.0f, 1.1f, -4.4f }, // eye
            rv_pdklib::rv_vec3{ 0.0f, 0.0f, 0.0f },                        // target
            rv_pdklib::rv_vec3{ 0.0f, 1.0f, 0.0f },                        // up
            1.0472f,                                                       // 60 deg vertical fov
            vw / vh, 0.1f, 100.0f);

    const rv_pdklib::rv_mat4 model = rv_pdklib::rv_mat4_mul(
        rv_pdklib::rv_mat4_rotate_y(spin_), rv_pdklib::rv_mat4_rotate_x(spin_ * 0.6f));

    const rv_pdklib::rv_xform_conf conf = rv_pdklib::rv_xform_conf_make(
        rv_pdklib::rv_camera_mvp(camera, model), camera, vw, vh, RV_DMAIN_DEPTH_ART_LO,
        RV_DMAIN_DEPTH_ART_HI, rv_pdklib::RV_CULL_SCREEN_CW);

    const rv_pdklib::rv_vec3 to_light{ -0.4f, 0.8f, -0.5f };

    for (const rv_dmain_face &face : RV_DMAIN_CUBE) {
        // Lighting is in WORLD space, so the normal takes the model matrix. The
        // positions do NOT: rv_camera_mvp already carries it, and transforming
        // them here as well would spin the cube twice.
        const rv_color shade = rv_pdklib::rv_shade_lambert(
            rv_pdklib::rv_hsv_to_rgb(hue_ + 0.5f, 0.45f, 1.0f),
            rv_pdklib::rv_mat4_mul_direction(model, face.normal), to_light, 0.25f);

        rv_pdklib::rv_xform_vertex vertexes[RV_DMAIN_QUAD_VERTICES];
        for (int i = 0; i < RV_DMAIN_QUAD_VERTICES; ++i) {
            vertexes[i].position = face.corner[i];
            vertexes[i].color = shade;
            vertexes[i].uv = rv_uv{ 0, 0 };
        }

        rv_primitive primitive{};
        if (!rv_pdklib::rv_xform_quad(conf, vertexes, primitive)) {
            continue; // culled or clipped
        }
        primitive.data.polygon.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;

        for (uint32_t i = 0; i < primitive.data.polygon.vertex_count; ++i) {
            primitive.data.polygon.vertexes[i].x =
                to_screen(static_cast<float>(primitive.data.polygon.vertexes[i].x + x));
            primitive.data.polygon.vertexes[i].y =
                to_screen(static_cast<float>(primitive.data.polygon.vertexes[i].y + y));
        }
        rv_cv_frame_put(cv, &primitive);
    }
}

void rv_dmain::draw_cell(int index)
{
    const int col = index % RV_DMAIN_GRID_COLS;
    const int row = index / RV_DMAIN_GRID_COLS;
    const int cx = RV_DMAIN_CELL_GRID_OFFSET_X + col * RV_DMAIN_CELL_W;
    const int cy = RV_DMAIN_GRID_TOP + row * RV_DMAIN_CELL_H;

    // The art area: the cell minus its label strip and a pixel of breathing room.
    const int ax = cx + RV_DMAIN_CELL_ART_OFFSET_X;
    const int ay = cy + RV_DMAIN_CELL_ART_TOP;
    const int aw = RV_DMAIN_CELL_W - RV_DMAIN_CELL_PADDING_RIGHT;
    const int ah = RV_DMAIN_CELL_H - RV_DMAIN_CELL_ART_TOP - RV_DMAIN_CELL_PADDING_BOTTOM;

    draw_cell_label(index, cx, cy);

    const rv_color hot = rv_pdklib::rv_hsv_to_rgb(hue_, 0.85f, 1.0f);
    const rv_color cold = rv_pdklib::rv_hsv_to_rgb(hue_ + 0.4f, 0.85f, 1.0f);
    const rv_color mid = rv_pdklib::rv_hsv_to_rgb(hue_ + 0.7f, 0.85f, 1.0f);

    draw_cell_art(index, ax, ay, aw, ah, hot, cold, mid);
}

void rv_dmain::draw_cell_label(int index, int cx, int cy)
{
    if (addr_font_ != 0) {
        rv_cv *cv = rv_pdko_cv(pdk_);
        const rv_pdklib::rv_font_style ink =
            rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_, RV_DMAIN_DEPTH_TEXT, 1);
        rv_pdklib::rv_font_draw(ink, cx + RV_DMAIN_CELL_LABEL_OFFSET_X, cy + 1,
            RV_DMAIN_CELL_LABEL[index], [cv](const rv_primitive &p) {
                rv_cv_frame_put(cv, &p);
            });
    }
}

void rv_dmain::draw_cell_art(
    int index, int ax, int ay, int aw, int ah, rv_color hot, rv_color cold, rv_color mid)
{
    rv_cv *cv = rv_pdko_cv(pdk_);

    switch (index) {
    case 0: { // LINE - the 1D case of colour interpolation
        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_LINE;
        primitive.depth = RV_DMAIN_DEPTH_ART_HI;
        primitive.data.line.vertexes[0] = make_vertex(ax, ay + ah, hot);
        primitive.data.line.vertexes[1] = make_vertex(ax + aw, ay, cold);
        rv_cv_frame_put(cv, &primitive);
        break;
    }
    case 1: { // TRI - gouraud across three different vertex colours
        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_POLYGON;
        primitive.depth = RV_DMAIN_DEPTH_ART_HI;
        rv_polygon &t = primitive.data.polygon;
        t.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        t.addr_texture = 0;
        t.addr_palette = 0;
        t.mapping = RV_TEXWRAP_CLAMP;
        t.vertex_count = RV_DMAIN_TRI_VERTICES;
        t.vertexes[0] = make_vertex(ax + aw / 2, ay, hot);
        t.vertexes[1] = make_vertex(ax, ay + ah, cold);
        t.vertexes[2] = make_vertex(ax + aw, ay + ah, mid);
        t.vertexes[3] = make_vertex(0, 0, hot); // ignored at vertex_count 3
        rv_cv_frame_put(cv, &primitive);
        break;
    }
    case 2: { // QUAD - the same fill across the (1,2,3)/(2,3,4) split
        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_POLYGON;
        primitive.depth = RV_DMAIN_DEPTH_ART_HI;
        rv_polygon &q = primitive.data.polygon;
        q.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        q.addr_texture = 0;
        q.addr_palette = 0;
        q.mapping = RV_TEXWRAP_CLAMP;
        q.vertex_count = RV_DMAIN_QUAD_VERTICES;
        // Z ORDER, not around the rim: the console splits a quad into
        // (1,2,3) and (2,3,4).
        q.vertexes[0] = make_vertex(ax, ay, hot);
        q.vertexes[1] = make_vertex(ax + aw, ay, cold);
        q.vertexes[2] = make_vertex(ax, ay + ah, mid);
        q.vertexes[3] = make_vertex(ax + aw, ay + ah, hot);
        rv_cv_frame_put(cv, &primitive);
        break;
    }
    case 3: { // WIRE - edges only, so the interior must stay background
        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_POLYGON;
        primitive.depth = RV_DMAIN_DEPTH_ART_HI;
        rv_polygon &q = primitive.data.polygon;
        q.fill_mode = RV_PRIMITIVE_FILL_MODE_WIREFRAME;
        q.addr_texture = 0;
        q.addr_palette = 0;
        q.mapping = RV_TEXWRAP_CLAMP;
        q.vertex_count = RV_DMAIN_QUAD_VERTICES;
        q.vertexes[0] = make_vertex(ax, ay, hot);
        q.vertexes[1] = make_vertex(ax + aw, ay, hot);
        q.vertexes[2] = make_vertex(ax, ay + ah, cold);
        q.vertexes[3] = make_vertex(ax + aw, ay + ah, cold);
        rv_cv_frame_put(cv, &primitive);
        break;
    }
    case 4: { // SPRITE - the axis-aligned fast path, one flat colour
        const rv_primitive primitive = make_bar(static_cast<float>(ax), static_cast<float>(ay),
            static_cast<float>(aw), static_cast<float>(ah), hot,
            RV_DMAIN_DEPTH_ART_HI);
        rv_cv_frame_put(cv, &primitive);
        break;
    }

    case 5: // DIRECT15 - a texel that carries its own colour, CLAMP
        draw_textured(ax, ay, aw, ah, addr_texture_, 0, RV_TEXWRAP_CLAMP);
        break;

    case 6: // TILE - the same texture, repeated
        draw_textured(ax, ay, aw, ah, addr_texture_, 0, RV_TEXWRAP_TILE);
        break;

    case 7: // IDX4 - an indexed texel resolved through a palette
        draw_textured(ax, ay, aw, ah, addr_idx4_, addr_idx4_palette_, RV_TEXWRAP_STRETCH);
        break;

    case 8: { // CUTOUT - 0000h writes neither colour nor depth
        // The backdrop is filed FIRST and NEARER-behind: what shows through
        // the holes is this magenta, and if the hole wrote depth it would
        // not.
        const rv_primitive primitive = make_bar(static_cast<float>(ax), static_cast<float>(ay),
            static_cast<float>(aw), static_cast<float>(ah),
            rv_color{ 220, 60, 200 }, RV_DMAIN_DEPTH_ART_LO);
        rv_cv_frame_put(cv, &primitive);
        draw_textured(ax, ay, aw, ah, addr_texture_, 0, RV_TEXWRAP_STRETCH);
        break;
    }
    case 9: { // DEPTH - the ordering table sorts, submission order does not
        // Filed nearest FIRST. If the console honoured submission order
        // instead of the depth key, the stack would come out inverted.
        const rv_color tint[] = { hot, cold, mid };
        const int32_t depth[] = { RV_DMAIN_DEPTH_ART_HI,
            RV_DMAIN_DEPTH_ART_HI - RV_DMAIN_DEPTH_STEP_OFFSET, RV_DMAIN_DEPTH_ART_LO };
        for (int i = 0; i < std::ssize(depth); ++i) {
            const rv_primitive primitive = make_bar(
                static_cast<float>(ax + i * RV_DMAIN_DEPTH_LADDER_STEP_X),
                static_cast<float>(ay + i * RV_DMAIN_DEPTH_LADDER_STEP_Y),
                static_cast<float>(aw - RV_DMAIN_DEPTH_LADDER_SHRINK_WIDTH),
                static_cast<float>(ah - RV_DMAIN_DEPTH_LADDER_SHRINK_HEIGHT), tint[i], depth[i]);
            rv_cv_frame_put(cv, &primitive);
        }
        break;
    }
    case 10: // STRETCH - uv ignored, the texture scaled to the primitive
        draw_textured(ax, ay, aw, ah, addr_texture_, 0, RV_TEXWRAP_STRETCH);
        break;

    default: // 3D - pdklib's transform feeding the console screen-space quads
        draw_cube_cell(ax, ay, aw, ah);
        break;
    }
}
void rv_dmain::draw_textured(int x, int y, int w, int h, int64_t addr_texture, int64_t addr_palette,
    rv_texture_mapping_type mapping)
{
    if (addr_texture == 0) {
        return;
    }

    rv_primitive primitive{};
    primitive.type = RV_PRIMITIVE_SPRITE;
    primitive.depth = RV_DMAIN_DEPTH_ART_HI;

    rv_sprite &sprite = primitive.data.sprite;
    sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE;
    sprite.addr_texture = addr_texture;
    sprite.addr_palette = addr_palette;
    sprite.color = rv_color{ 255, 255, 255 };
    sprite.mapping = mapping;
    sprite.x = to_screen(static_cast<float>(x));
    sprite.y = to_screen(static_cast<float>(y));
    sprite.width = static_cast<uint16_t>(w);
    sprite.height = static_cast<uint16_t>(h);

    rv_cv_frame_put(rv_pdko_cv(pdk_), &primitive);
}

} // namespace rv_service
