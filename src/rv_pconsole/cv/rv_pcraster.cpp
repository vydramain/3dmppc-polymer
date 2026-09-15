// rv_pcraster: colour packing, lines, sprites and the dispatch; polygons live in rv_pcraster_poly.cpp.
#include "rv_pconsole/cv/rv_pcraster.hpp"

#include "pdklib/rv_textures/rv_texel_pack.hpp"
#include "rv_pconsole/cv/rv_pcraster_pixel.hpp"

#include <cmath>

namespace rv_3dmppc
{

namespace
{

int64_t abs64(int64_t value)
{
    return value < 0 ? -value : value;
}

rv_color lerp_color(const rv_color &a, const rv_color &b, double t)
{
    rv_color out;
    out.r = clamp_channel(static_cast<double>(a.r) + (static_cast<double>(b.r) - a.r) * t);
    out.g = clamp_channel(static_cast<double>(a.g) + (static_cast<double>(b.g) - a.g) * t);
    out.b = clamp_channel(static_cast<double>(a.b) + (static_cast<double>(b.b) - a.b) * t);
    return out;
}

} // namespace

uint16_t rv_pcraster::pack_rgb555(rv_color color)
{
    // Round-to-nearest. Used for flat, position-independent conversions such as
    // the frame clear colour, where a dither pattern would just be noise on an
    // untextured background.
    return rv_pdklib::rv_texel_pack(rv_pdklib::rv_texel_quantize(color));
}

void rv_pcraster::draw_line(rv_pcfbuf &fbuf, const rv_line &line, int32_t depth, bool z_enabled)
{
    const rv_vertex &a = line.vertexes[0];
    const rv_vertex &b = line.vertexes[1];

    const int64_t x0 = a.x;
    const int64_t y0 = a.y;
    const int64_t dx = static_cast<int64_t>(b.x) - x0;
    const int64_t dy = static_cast<int64_t>(b.y) - y0;

    // DDA - parameterize the segment as p(t) = p0 + t * (p1 - p0) and
    // step t in max(|dx|, |dy|) equal increments. Choosing the longer axis as
    // the driving one is what makes the trace gapless: the fast axis advances by
    // at most one pixel per step, so consecutive samples are always neighbours.
    // Bresenham would avoid the floating point, but t is needed anyway to
    // interpolate the two vertex colours along the segment, so the parametric
    // form is the cheaper of the two here.
    const int64_t steps = max64(abs64(dx), abs64(dy));
    if (steps == 0) {
        if (fbuf.inside(x0, y0)) {
            emit(fbuf, x0, y0, pack_rgb555_dithered(a.color, x0, y0), depth, z_enabled);
        }
        return;
    }

    const double inv_steps = 1.0 / static_cast<double>(steps);
    for (int64_t i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) * inv_steps;
        const int64_t x = x0 + static_cast<int64_t>(std::llround(static_cast<double>(dx) * t));
        const int64_t y = y0 + static_cast<int64_t>(std::llround(static_cast<double>(dy) * t));

        // A line is a curve, not a box: clipping the endpoints would move them
        // and shear the interpolation, so the trace is clipped per sample.
        if (!fbuf.inside(x, y)) {
            continue;
        }
        const rv_color color = lerp_color(a.color, b.color, t);
        emit(fbuf, x, y, pack_rgb555_dithered(color, x, y), depth, z_enabled);
    }
}

void rv_pcraster::draw_sprite(rv_pcfbuf &fbuf, const rv_sprite &sprite, const rv_pctexview &texture,
    int32_t depth, bool z_enabled)
{
    if (sprite.width == 0 || sprite.height == 0) {
        return;
    }

    const int64_t x0 = sprite.x;
    const int64_t y0 = sprite.y;
    const int64_t x1 = x0 + static_cast<int64_t>(sprite.width);  // exclusive
    const int64_t y1 = y0 + static_cast<int64_t>(sprite.height); // exclusive

    // Bounding box clipped to the screen again - a sprite is axis-aligned by construction, so
    // the box IS the primitive and the clip is exact rather than conservative.
    const int64_t cx0 = max64(x0, 0);
    const int64_t cy0 = max64(y0, 0);
    const int64_t cx1 = min64(x1, fbuf.width());
    const int64_t cy1 = min64(y1, fbuf.height());
    if (cx0 >= cx1 || cy0 >= cy1) {
        return;
    }

    // A sprite that asked to sample but whose view came back invalid (a region
    // that was allocated and never uploaded into) falls back to the flat fill
    // below: a solid rectangle in the wrong colour is a far better bug report
    // than a primitive that silently disappears.
    const bool textured =
        sprite.fill_mode == RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE && texture.valid();

    if (textured) {
        // The texture is laid down from the sprite's UPPER-LEFT corner, one
        // texel per pixel - the contract's rule (rv_primitives.hpp), and the
        // reason a sprite carries no uv at all. CLAMP smears the last texel over
        // the overhang, TILE repeats; both are the sampler's job.
        //
        // STRETCH rescales instead: the whole texture is fitted onto the
        // sprite's own (unclipped) extent, so a clipped sprite keeps its scale.
        int64_t du_dx = RV_UV_FX_ONE;
        int64_t dv_dy = RV_UV_FX_ONE;
        if (sprite.mapping == RV_TEXWRAP_STRETCH) {
            du_dx = fx_ratio(texture.width, static_cast<int64_t>(sprite.width));
            dv_dy = fx_ratio(texture.height, static_cast<int64_t>(sprite.height));
        }

        int64_t v_row = (cy0 - y0) * dv_dy;
        const int64_t u_start = (cx0 - x0) * du_dx;

        for (int64_t y = cy0; y < cy1; ++y) {
            int64_t u_fx = u_start;
            for (int64_t x = cx0; x < cx1; ++x) {
                const rv_pctexel_sample texel = rv_pctexel::sample(
                    texture, u_fx >> RV_UV_FX_SHIFT, v_row >> RV_UV_FX_SHIFT, sprite.mapping);
                if (texel.drawn) {
                    emit(fbuf, x, y, dither_rgb555(texel.value, x, y), depth, z_enabled);
                }
                u_fx += du_dx;
            }
            v_row += dv_dy;
        }
        return;
    }

    const bool wireframe = sprite.fill_mode == RV_PRIMITIVE_FILL_MODE_WIREFRAME;

    for (int64_t y = cy0; y < cy1; ++y) {
        if (wireframe && y != y0 && y != y1 - 1) {
            // Interior row: only the two vertical sides, and only if the
            // unclipped side actually survived the clip.
            if (x0 >= cx0 && x0 < cx1) {
                emit(fbuf, x0, y, pack_rgb555_dithered(sprite.color, x0, y), depth, z_enabled);
            }
            if (x1 - 1 >= cx0 && x1 - 1 < cx1) {
                emit(fbuf, x1 - 1, y, pack_rgb555_dithered(sprite.color, x1 - 1, y), depth,
                    z_enabled);
            }
            continue;
        }

        for (int64_t x = cx0; x < cx1; ++x) {
            emit(fbuf, x, y, pack_rgb555_dithered(sprite.color, x, y), depth, z_enabled);
        }
    }
}

void rv_pcraster::draw(rv_pcfbuf &fbuf, const rv_primitive &primitive, const rv_pctexview &texture,
    bool z_enabled)
{
    // Dispatch on the variant tag. rv_primitive is a tagged union (the
    // PDK is a C-shaped ABI, so no std::variant), and this is the one place that
    // reads the tag.
    switch (primitive.type) {
    case RV_PRIMITIVE_LINE:
        draw_line(fbuf, primitive.data.line, primitive.depth, z_enabled);
        break;
    case RV_PRIMITIVE_POLYGON:
        draw_polygon(fbuf, primitive.data.polygon, texture, primitive.depth, z_enabled);
        break;
    case RV_PRIMITIVE_SPRITE:
        draw_sprite(fbuf, primitive.data.sprite, texture, primitive.depth, z_enabled);
        break;
    default:
        break;
    }
}

} // namespace rv_3dmppc
