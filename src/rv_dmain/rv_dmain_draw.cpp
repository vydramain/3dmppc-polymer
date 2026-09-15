// rv_dmain: per-frame drawing of the test grid and the status POST.
#include "rv_dmain.hpp"

#include <cmath>
#include <cstdio>

#include "pdk/cv/rv_cv.h"
#include "pdklib/rv_camera/rv_camera.hpp"
#include "pdklib/rv_color/rv_color.hpp"
#include "pdklib/rv_math/rv_math.hpp"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_math/rv_xform.hpp"

namespace rv_service
{

namespace
{

// Ordering-table probes. Larger depth = nearer, so the wrap row sits behind
// the cube and the status bars sit in front of it.
constexpr int32_t RV_DMAIN_DEPTH_ART_LO = -400;
constexpr int32_t RV_DMAIN_DEPTH_ART_HI = 400;
constexpr int32_t RV_DMAIN_DEPTH_PANEL = 880; // the slab text sits on
constexpr int32_t RV_DMAIN_DEPTH_TEXT = 900;  // over everything, it explains it

// The screen is a POST: it reports WHAT THE MACHINE IS and whether each part
// answered - not a caption for what is drawn below it.
//
// The first version of this screen labelled the picture ("textures", "cube",
// "ticks", "bar", "south") and was useless: those are the names of THIS FILE's
// implementation details, and nobody outside it can know that "south" is what
// the input contract calls the bottom face button. Every number below instead
// comes from a real query - screen_width(), voice_count(), card_slots() - so
// the fact that it is on screen at all is itself the proof that the query
// answers.
constexpr const char *RV_DMAIN_TITLE = "3DMPPC";
constexpr const char *RV_DMAIN_NO_DISC = "NO DISC INSERTED";
// Named by the KEY a person can press. "south" is what the input contract calls
// the bottom face button, and nobody outside that header knows it.
constexpr const char *RV_DMAIN_HINTS = "Z sound   ESC power off";

// Human sizes. 1048576 reads as "1 MB", 524288 as "512 KB" - a service screen
// that prints raw byte counts makes the reader do arithmetic to learn something
// the machine already knows.
void format_bytes(char *out, std::size_t cap, int64_t bytes)
{
    // The buffer must fit the widest int64 plus a unit; snprintf truncating a
    // diagnostic silently is the last thing a diagnostic screen should do.
    if (bytes >= 1024 * 1024 && bytes % (1024 * 1024) == 0) {
        std::snprintf(out, cap, "%lld MB", static_cast<long long>(bytes / (1024 * 1024)));
    } else if (bytes >= 1024) {
        std::snprintf(out, cap, "%lld KB", static_cast<long long>(bytes / 1024));
    } else {
        std::snprintf(out, cap, "%lld B", static_cast<long long>(bytes));
    }
}

// The unit cube: six quads, each in the PDK's Z ORDER, because the console
// splits a quad into triangles (1,2,3) and (2,3,4) rather than walking the rim.
// Wound counter-clockwise as seen from OUTSIDE, which is what
// RV_CULL_SCREEN_CW keeps.
struct rv_dmain_face {
    rv_pdklib::rv_vec3 corner[4];
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

int16_t to_screen(float value)
{
    const float rounded = std::floor(value + 0.5f);
    if (rounded <= -32768.0f) {
        return -32768;
    }
    if (rounded >= 32767.0f) {
        return 32767;
    }
    return static_cast<int16_t>(rounded);
}

// A flat rectangle, filled in completely so no byte of the union is left
// indeterminate. Factory method - rv_primitive is a tagged union of
// constructor-less structs, and an "almost filled" literal leaves live fields
// holding whatever the stack had.
rv_primitive make_bar(float x, float y, float w, float h, rv_color color, int32_t depth)
{
    rv_primitive primitive{};
    primitive.type = RV_PRIMITIVE_SPRITE;
    primitive.depth = depth;

    rv_sprite &sprite = primitive.data.sprite;
    sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
    sprite.addr_texture = 0;
    sprite.addr_palette = 0;
    sprite.color = color;
    sprite.mapping = RV_TEXWRAP_CLAMP;
    sprite.x = to_screen(x);
    sprite.y = to_screen(y);
    sprite.width = static_cast<uint16_t>(w < 0.0f ? 0.0f : w);
    sprite.height = static_cast<uint16_t>(h < 0.0f ? 0.0f : h);
    return primitive;
}

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

        rv_pdklib::rv_xform_vertex vertexes[4];
        for (int i = 0; i < 4; ++i) {
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
    rv_cv *cv = rv_pdko_cv(pdk_);

    const int col = index % RV_DMAIN_GRID_COLS;
    const int row = index / RV_DMAIN_GRID_COLS;
    const int cx = 4 + col * RV_DMAIN_CELL_W;
    const int cy = RV_DMAIN_GRID_TOP + row * RV_DMAIN_CELL_H;

    // The art area: the cell minus its label strip and a pixel of breathing room.
    const int ax = cx + 3;
    const int ay = cy + RV_DMAIN_CELL_ART_TOP;
    const int aw = RV_DMAIN_CELL_W - 8;
    const int ah = RV_DMAIN_CELL_H - RV_DMAIN_CELL_ART_TOP - 5;

    if (addr_font_ != 0) {
        const rv_pdklib::rv_font_style ink =
            rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_, RV_DMAIN_DEPTH_TEXT, 1);
        rv_pdklib::rv_font_draw(ink, cx + 2, cy + 1, RV_DMAIN_CELL_LABEL[index],
            [cv](const rv_primitive &p) {
                rv_cv_frame_put(cv, &p);
            });
    }

    const rv_color hot = rv_pdklib::rv_hsv_to_rgb(hue_, 0.85f, 1.0f);
    const rv_color cold = rv_pdklib::rv_hsv_to_rgb(hue_ + 0.4f, 0.85f, 1.0f);
    const rv_color mid = rv_pdklib::rv_hsv_to_rgb(hue_ + 0.7f, 0.85f, 1.0f);

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
        t.vertex_count = 3;
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
        q.vertex_count = 4;
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
        q.vertex_count = 4;
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
        const rv_color tint[3] = { hot, cold, mid };
        const int32_t depth[3] = { RV_DMAIN_DEPTH_ART_HI, RV_DMAIN_DEPTH_ART_HI - 40,
            RV_DMAIN_DEPTH_ART_LO };
        for (int i = 0; i < 3; ++i) {
            const rv_primitive primitive = make_bar(static_cast<float>(ax + i * 12),
                static_cast<float>(ay + i * 8), static_cast<float>(aw - 24),
                static_cast<float>(ah - 16), tint[i], depth[i]);
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

// POST header. Two lines that say what this machine IS - the virtual
// budget it imposes on itself - and one bottom line saying whether each
// subsystem answered. The numbers come straight from the contract's geometry
// queries, so a line that prints at all is a query that worked.
//
// An earlier version of this screen captioned the picture instead ("textures",
// "cube", "ticks", "bar", "south") and was useless: those are the names of THIS
// file's internals, and nobody outside it can know that "south" is what the
// input contract calls the bottom face button.
void rv_dmain::draw_post_row(int, const char *, const char *, const char *, bool)
{
}

void rv_dmain::draw_post()
{
    if (addr_font_ == 0) {
        return;
    }

    rv_cv *cv = rv_pdko_cv(pdk_);
    auto file = [cv](const rv_primitive &primitive) {
        rv_cv_frame_put(cv, &primitive);
    };

    const int width = static_cast<int>(screen_width_);
    const int height = static_cast<int>(screen_height_);
    const rv_color slab{ 12, 14, 22 };
    const rv_pdklib::rv_font_style ink =
        rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_, RV_DMAIN_DEPTH_TEXT, 1);
    const rv_pdklib::rv_font_style bad =
        rv_pdklib::rv_font_style_make(addr_font_, addr_font_palette_bad_, RV_DMAIN_DEPTH_TEXT, 1);

    // Opaque slabs, not a dim overlay: the console has no blending, and light
    // ink over lit geometry is exactly what made the first version unreadable.
    const rv_primitive top_slab =
        make_bar(0.0f, 0.0f, static_cast<float>(width), 24.0f, slab, RV_DMAIN_DEPTH_PANEL);
    rv_cv_frame_put(cv, &top_slab);
    rv_pdklib::rv_font_draw(ink, 4, 2, RV_DMAIN_TITLE, file);
    rv_pdklib::rv_font_draw(ink, width - 4 - rv_pdklib::rv_font_measure_width(RV_DMAIN_NO_DISC, 1),
        2, RV_DMAIN_NO_DISC, file);

    // The budget line. Saying VIRTUAL out loud matters: none of this is what the
    // host has, all of it is what the fantasy machine is defined to have, and
    // the pools really do refuse past the line.
    char budget[96];
    char vram[24];
    char sram[24];
    format_bytes(vram, sizeof(vram), video_memory_size_);
    format_bytes(sram, sizeof(sram), sound_memory_size_);
    std::snprintf(budget, sizeof(budget), "VIRTUAL %lldx%lld %s %lldv %s",
        static_cast<long long>(screen_width_), static_cast<long long>(screen_height_),
        vram, static_cast<long long>(voice_count_), sram);
    rv_pdklib::rv_font_draw(ink, 4, 13, budget, file);

    // The bottom slab: one word per subsystem, and the word is the whole report.
    const rv_primitive bottom_slab = make_bar(0.0f, static_cast<float>(height) - 24.0f,
        static_cast<float>(width), 24.0f, slab, RV_DMAIN_DEPTH_PANEL);
    rv_cv_frame_put(cv, &bottom_slab);

    struct rv_dmain_probe {
        const char *label;
        bool ok;
        bool absent; // legal "nothing there", which is not a failure
    };
    const rv_dmain_probe probes[4] = {
        { "AUDIO", beep_ok_, false },
        { "DRIVE", asset_ok_, asset_bytes_ == 0 },
        { "CARD", card_ok_, false },
        { "PATHS", drive_rejects_paths_, false },
    };

    int pen = 4;
    for (const rv_dmain_probe &probe : probes) {
        rv_pdklib::rv_font_draw(ink, pen, static_cast<int>(height) - 21, probe.label, file);
        pen += rv_pdklib::rv_font_measure_width(probe.label, 1) + 8;

        const char *mark = probe.absent ? "-" : (probe.ok ? "OK" : "FAIL");
        rv_pdklib::rv_font_draw(probe.ok || probe.absent ? ink : bad, pen,
            static_cast<int>(height) - 21, mark, file);
        pen += rv_pdklib::rv_font_measure_width(mark, 1) + 10;
    }

    // The probe row owns its whole line: four labels and four verdicts already
    // fill 40 columns, and anything sharing the row lands on top of them.
    char pads[32];
    std::snprintf(pads, sizeof(pads), "%lld pad(s) boot %lu",
        static_cast<long long>(pads_connected_), static_cast<unsigned long>(boot_count_));
    rv_pdklib::rv_font_draw(ink, 4, height - 11, pads, file);
    rv_pdklib::rv_font_draw(ink, width - 4 - rv_pdklib::rv_font_measure_width(RV_DMAIN_HINTS, 1),
        height - 11, RV_DMAIN_HINTS, file);
}

} // namespace rv_service
