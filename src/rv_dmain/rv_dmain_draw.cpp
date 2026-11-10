// rv_dmain: per-frame drawing of the test grid.
#include "rv_dmain.hpp"

#include "pdk/cv/rv_cv.h"
#include "pdklib/rv_camera/rv_camera.hpp"
#include "pdklib/rv_color/rv_color.hpp"
#include "pdklib/rv_math/rv_math.hpp"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_xform.hpp"

#include "rv_dmain_draw_shared.hpp"
#include "rv_dmain_ui.hpp"

namespace rv_service
{

namespace
{

// Ordering-table probes. Larger depth = nearer, so the wrap row sits behind
// the cube and the status bars sit in front of it.
constexpr int32_t RV_DMAIN_DEPTH_ART_LO = -400;
constexpr int32_t RV_DMAIN_DEPTH_ART_HI = 400;

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
static_assert(std::size(RV_DMAIN_CUBE) == UI_CUBE_FACES, "one quad per cube face");

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

constexpr int RV_DMAIN_TILE_MARGIN_X = 2;                          // tile inset from the cell's left and right edges
constexpr int RV_DMAIN_CAPTION_GAP = 2;                            // tile bottom to caption top
constexpr int RV_DMAIN_CAPTION_H = rv_pdklib::rv_font_cell_height; // one text line at scale 1
constexpr int RV_DMAIN_CAPTION_SPARE = 1;                          // below the caption
constexpr int RV_DMAIN_TILE_BELOW = RV_DMAIN_CAPTION_GAP + RV_DMAIN_CAPTION_H + RV_DMAIN_CAPTION_SPARE; // cell bottom strip
constexpr int RV_DMAIN_DEPTH_STEP_OFFSET = 40;
constexpr int RV_DMAIN_DEPTH_LADDER_STEP_X = 12;
constexpr int RV_DMAIN_DEPTH_LADDER_STEP_Y = 8;
constexpr int RV_DMAIN_DEPTH_LADDER_SHRINK_WIDTH = 24;
constexpr int RV_DMAIN_DEPTH_LADDER_SHRINK_HEIGHT = 16;

// The raised tile inside a grid cell; the caption goes in the strip below it.
constexpr rv_dmain_detail::rv_ui_rect cell_tile(int cx, int cy, int cw, int ch)
{
    return rv_dmain_detail::rv_ui_rect{ cx + RV_DMAIN_TILE_MARGIN_X,
        cy,
        cw - 2 * RV_DMAIN_TILE_MARGIN_X,
        ch - RV_DMAIN_TILE_BELOW };
}

} // namespace

void rv_dmain::draw_test_grid()
{
    using namespace rv_dmain_detail;
    const rv_ui_rect grid = ui_grid(ui_window(static_cast<int>(screen_width_), static_cast<int>(screen_height_)));
    const int cw = grid.w / UI_GRID_COLS;
    const int ch = grid.h / UI_GRID_ROWS;
    const rv_ui_rect probe = cell_tile(grid.x, grid.y, cw, ch);
    if (probe.w - 2 * UI_BEVEL < 1 || probe.h - 2 * UI_BEVEL < 1) {
        if (!grid_warned_) {
            grid_warned_ = true;
            RV_LOG_WARN("grid", "screen {}x{} is too small, the test tiles are not drawn", screen_width_, screen_height_);
        }
        return;
    }

    for (int i = 0; i < UI_GRID_COLS * UI_GRID_ROWS; ++i) {
        const rv_ui_rect cell{ grid.x + i % UI_GRID_COLS * cw, grid.y + i / UI_GRID_COLS * ch, cw, ch };
        draw_cell(i, cell, cell_tile(cell.x, cell.y, cw, ch));
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

    const rv_pdklib::rv_camera camera = rv_pdklib::rv_camera_make(rv_pdklib::rv_vec3{ 0.0f, 1.1f, -4.4f }, // eye
        rv_pdklib::rv_vec3{ 0.0f, 0.0f, 0.0f },                                                            // target
        rv_pdklib::rv_vec3{ 0.0f, 1.0f, 0.0f },                                                            // up
        1.0472f, // 60 deg vertical fov
        vw / vh,
        0.1f,
        100.0f);

    const rv_pdklib::rv_mat4 model =
        rv_pdklib::rv_mat4_mul(rv_pdklib::rv_mat4_rotate_y(spin_), rv_pdklib::rv_mat4_rotate_x(spin_ * 0.6f));

    const rv_pdklib::rv_xform_conf conf = rv_pdklib::rv_xform_conf_make(rv_pdklib::rv_camera_mvp(camera, model),
        camera,
        vw,
        vh,
        RV_DMAIN_DEPTH_ART_LO,
        RV_DMAIN_DEPTH_ART_HI,
        rv_pdklib::RV_CULL_SCREEN_CW);

    const rv_pdklib::rv_vec3 to_light{ -0.4f, 0.8f, -0.5f };

    for (const rv_dmain_face &face : RV_DMAIN_CUBE) {
        // Lighting is in WORLD space, so the normal takes the model matrix. The
        // positions do NOT: rv_camera_mvp already carries it, and transforming
        // them here as well would spin the cube twice.
        const rv_color shade = rv_pdklib::rv_shade_lambert(rv_pdklib::rv_hsv_to_rgb(hue_ + 0.5f, 0.45f, 1.0f),
            rv_pdklib::rv_mat4_mul_direction(model, face.normal),
            to_light,
            0.25f);

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
            primitive.data.polygon.vertexes[i].x = to_screen(static_cast<float>(primitive.data.polygon.vertexes[i].x + x));
            primitive.data.polygon.vertexes[i].y = to_screen(static_cast<float>(primitive.data.polygon.vertexes[i].y + y));
        }
        rv_cv_frame_put(cv, &primitive);
    }
}

void rv_dmain::draw_cell(int index, const rv_dmain_detail::rv_ui_rect &cell, const rv_dmain_detail::rv_ui_rect &tile)
{
    using namespace rv_dmain_detail;
    rv_cv *cv = rv_pdko_cv(pdk_);
    ui_plate_raised(rv_pdklib::rv_theme_dark, tile, UI_DEPTH_TILE, [cv](const rv_primitive &p) {
        rv_cv_frame_put(cv, &p);
    });

    // The art area: the tile inside its bevel.
    const int ax = tile.x + UI_BEVEL;
    const int ay = tile.y + UI_BEVEL;
    const int aw = tile.w - 2 * UI_BEVEL;
    const int ah = tile.h - 2 * UI_BEVEL;

    draw_cell_label(index, cell, tile);

    const rv_color hot = rv_pdklib::rv_hsv_to_rgb(hue_, 0.85f, 1.0f);
    const rv_color cold = rv_pdklib::rv_hsv_to_rgb(hue_ + 0.4f, 0.85f, 1.0f);
    const rv_color mid = rv_pdklib::rv_hsv_to_rgb(hue_ + 0.7f, 0.85f, 1.0f);

    draw_cell_art(index, ax, ay, aw, ah, hot, cold, mid);
}

// The caption centred by ink width under the tile; the cell is its fit area. A caption that does not fit
// the cell is dropped without a log (it would repeat every frame); the tile still shows the test.
void rv_dmain::draw_cell_label(int index, const rv_dmain_detail::rv_ui_rect &cell, const rv_dmain_detail::rv_ui_rect &tile)
{
    if (addr_font_ == 0) {
        return;
    }
    using namespace rv_dmain_detail;
    rv_cv *cv = rv_pdko_cv(pdk_);
    const std::string_view caption = UI_TEST_CAPTIONS[index];
    const rv_pdklib::rv_font_style ink =
        rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_[FONT_INK_TEXT], UI_DEPTH_TEXT, 1);
    ui_text(ink,
        cell,
        tile.x + (tile.w - ui_ink_width(caption, 1)) / 2,
        tile.y + tile.h + RV_DMAIN_CAPTION_GAP,
        caption,
        [cv](const rv_primitive &p) {
            rv_cv_frame_put(cv, &p);
        });
}

void rv_dmain::draw_cell_art(int index, int ax, int ay, int aw, int ah, rv_color hot, rv_color cold, rv_color mid)
{
    rv_cv *cv = rv_pdko_cv(pdk_);

    switch (index) {
    case 0: { // LINE - the 1D case of colour interpolation
        rv_primitive primitive{};
        primitive.type = RV_PRIMITIVE_LINE;
        primitive.depth = RV_DMAIN_DEPTH_ART_HI;
        // A line includes both end pixels: the far end is the last pixel inside the art area.
        primitive.data.line.vertexes[0] = make_vertex(ax, ay + ah - 1, hot);
        primitive.data.line.vertexes[1] = make_vertex(ax + aw - 1, ay, cold);
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
        // Edges include their end pixels: the far edges are the last pixels inside the art area.
        q.vertexes[1] = make_vertex(ax + aw - 1, ay, hot);
        q.vertexes[2] = make_vertex(ax, ay + ah - 1, cold);
        q.vertexes[3] = make_vertex(ax + aw - 1, ay + ah - 1, cold);
        rv_cv_frame_put(cv, &primitive);
        break;
    }
    case 4: { // SPRITE - the axis-aligned fast path, one flat colour
        const rv_primitive primitive = make_bar(static_cast<float>(ax),
            static_cast<float>(ay),
            static_cast<float>(aw),
            static_cast<float>(ah),
            hot,
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
        const rv_primitive primitive = make_bar(static_cast<float>(ax),
            static_cast<float>(ay),
            static_cast<float>(aw),
            static_cast<float>(ah),
            rv_color{ 220, 60, 200 },
            RV_DMAIN_DEPTH_ART_LO);
        rv_cv_frame_put(cv, &primitive);
        draw_textured(ax, ay, aw, ah, addr_texture_, 0, RV_TEXWRAP_STRETCH);
        break;
    }
    case 9: { // DEPTH - the ordering table sorts, submission order does not
        // Filed nearest FIRST. If the console honoured submission order
        // instead of the depth key, the stack would come out inverted.
        const rv_color tint[] = { hot, cold, mid };
        const int32_t depth[] = { RV_DMAIN_DEPTH_ART_HI,
            RV_DMAIN_DEPTH_ART_HI - RV_DMAIN_DEPTH_STEP_OFFSET,
            RV_DMAIN_DEPTH_ART_LO };
        static_assert(std::size(depth) == UI_DEPTH_LADDER_BARS, "one bar per ladder rung");
        for (int i = 0; i < std::ssize(depth); ++i) {
            const rv_primitive primitive = make_bar(static_cast<float>(ax + i * RV_DMAIN_DEPTH_LADDER_STEP_X),
                static_cast<float>(ay + i * RV_DMAIN_DEPTH_LADDER_STEP_Y),
                static_cast<float>(aw - RV_DMAIN_DEPTH_LADDER_SHRINK_WIDTH),
                static_cast<float>(ah - RV_DMAIN_DEPTH_LADDER_SHRINK_HEIGHT),
                tint[i],
                depth[i]);
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
void rv_dmain::draw_textured(int x,
    int y,
    int w,
    int h,
    int64_t addr_texture,
    int64_t addr_palette,
    rv_texture_mapping_type mapping)
{
    // Empty on a refused texture load: the failure is not drawn here, its outcome
    // is the TEX row of draw_post(), and its code is in the WARN of the setup step.
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
