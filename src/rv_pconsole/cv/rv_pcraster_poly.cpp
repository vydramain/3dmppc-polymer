// rv_pcraster: polygons - edge functions, uv gradients and the triangle fill.
#include "rv_pconsole/cv/rv_pcraster.hpp"

#include "rv_pconsole/cv/rv_pcraster_pixel.hpp"

namespace rv_3dmppc
{

namespace
{

// Edge function (half-plane test, Pineda 1988) -
//   E(p) = (p.x - x0) * (y1 - y0) - (p.y - y0) * (x1 - x0)
// is the 2D cross product of the edge vector with the vector to p, i.e. twice the
// signed area of the triangle (v0, v1, p). Its SIGN says which side of the
// infinite line through v0->v1 the point lies on, so "inside the triangle" is
// three sign tests and nothing else - no slopes, no division, no special case for
// horizontal edges or for which vertex is topmost.
//
// The property the inner loop is built on: E is AFFINE in p. Therefore
//   E(x + 1, y) = E(x, y) + (y1 - y0)   and   E(x, y + 1) = E(x, y) - (x1 - x0),
// so after one setup per triangle each pixel step costs a single integer add per
// edge. Evaluated in int64 because screen coordinates are int16 and the products
// above reach ~2^32, which would silently overflow int32 for an off-screen vertex.
int64_t edge_at(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t px, int64_t py)
{
    return (px - x0) * (y1 - y0) - (py - y0) * (x1 - x0);
}

// Top-left fill rule - a pixel exactly ON a shared edge satisfies E == 0
// for BOTH triangles that meet there. Accepting it in both draws it twice (a
// visible seam once blending lands, and a wasted write now); rejecting it in both
// leaves a one-pixel crack. The fix is a tie-break that depends only on the edge's
// DIRECTION: accept E == 0 only on edges that are "top" (horizontal with the
// interior below) or "left" (the interior to their right). With the winding
// normalized (positive doubled area) the interior lies where E > 0, so the
// interior direction is the gradient (dy, -dx): "top" is dy == 0 && dx < 0, and
// "left" is dy > 0.
//
// The guarantee: a shared edge is traversed in OPPOSITE directions by the two
// triangles, and is_top_left(d) != is_top_left(-d) for every non-degenerate d, so
// exactly one of the two claims the boundary pixel. Critical for quads, whose two
// halves share the (2,3) diagonal by construction.
bool is_top_left(int64_t dx, int64_t dy)
{
    return dy > 0 || (dy == 0 && dx < 0);
}

// Where a primitive's texels come from, resolved by the caller. A null (or
// invalid) `view` means "fill with colour" and is the whole of the untextured
// path - no second code path, no flag to keep in sync.
//
// The box is the primitive's own bounding box in screen space, UNCLIPPED: it is
// what RV_TEXWRAP_STRETCH stretches the texture across, and taking the clipped
// box instead would rescale the texture whenever the primitive touched a screen
// edge.
struct rv_pctexstage {
    const rv_pctexview *view = nullptr;
    rv_texture_mapping_type mapping = RV_TEXWRAP_CLAMP;

    int64_t box_x = 0;
    int64_t box_y = 0;
    int64_t box_w = 0;
    int64_t box_h = 0;

    bool active() const
    {
        return view != nullptr && view->valid();
    }
    bool stretch() const
    {
        return mapping == RV_TEXWRAP_STRETCH;
    }
};

// A 16.16 texture coordinate and its two screen-space gradients. `u`/`v` hold
// the value at the start of the current row; the row loop steps them by the d/dy
// pair, the pixel loop by the d/dx pair.
struct rv_pcuvwalk {
    int64_t u = 0;
    int64_t v = 0;
    int64_t du_dx = 0;
    int64_t dv_dx = 0;
    int64_t du_dy = 0;
    int64_t dv_dy = 0;
};

struct rv_pctri {
    int64_t x[3];
    int64_t y[3];
    rv_color color[3];
    rv_uv uv[3];
};

void fill_triangle(rv_pcfbuf &fbuf, rv_pctri tri, const rv_pctexstage &stage, int32_t depth,
    bool z_enabled)
{
    // Signed area - area2 = E(v0, v1, v2) is twice the signed area of
    // the triangle. Its sign is the winding; a negative area means every edge
    // function has the opposite sense and the "all E >= 0" test would reject the
    // whole interior. Swapping any two vertices flips the winding, so one
    // conditional swap normalizes every input to the positive case and the inner
    // loop needs no orientation branch.
    //
    // Note what this deliberately does NOT do: back-face culling. The winding is
    // normalized, never rejected - the console draws whatever the disc hands it
    // (the contract names no facing rule, and a disc that wants culling does it
    // in its own transform stage where it still has a normal to test).
    int64_t area2 = edge_at(tri.x[0], tri.y[0], tri.x[1], tri.y[1], tri.x[2], tri.y[2]);
    if (area2 == 0) {
        return; // degenerate: zero-area triangles cover no pixel centre
    }
    if (area2 < 0) {
        const int64_t sx = tri.x[1];
        const int64_t sy = tri.y[1];
        const rv_color sc = tri.color[1];
        const rv_uv suv = tri.uv[1];
        tri.x[1] = tri.x[2];
        tri.y[1] = tri.y[2];
        tri.color[1] = tri.color[2];
        tri.uv[1] = tri.uv[2];
        tri.x[2] = sx;
        tri.y[2] = sy;
        tri.color[2] = sc;
        tri.uv[2] = suv;
        area2 = -area2;
    }

    // Bounding box clipped to the screen - the triangle covers no pixel outside the
    // box spanned by its vertices, and the console has no scissor state beyond
    // the screen itself (rv_cv: "a frame is always the whole screen"). So the
    // intersection of the two rectangles is the complete clip: no polygon
    // clipping against the frustum, no per-pixel bounds test in the inner loop,
    // and an entirely off-screen primitive costs only this comparison.
    int64_t min_x = max64(min64(tri.x[0], min64(tri.x[1], tri.x[2])), 0);
    int64_t min_y = max64(min64(tri.y[0], min64(tri.y[1], tri.y[2])), 0);
    int64_t max_x = min64(max64(tri.x[0], max64(tri.x[1], tri.x[2])), fbuf.width() - 1);
    int64_t max_y = min64(max64(tri.y[0], max64(tri.y[1], tri.y[2])), fbuf.height() - 1);
    if (min_x > max_x || min_y > max_y) {
        return;
    }

    // Edge i runs from vertex i to vertex (i + 1) % 3; the vertex it does NOT
    // touch is (i + 2) % 3.
    int64_t step_x[3];
    int64_t step_y[3];
    int64_t bias[3];
    int64_t row[3];
    for (int i = 0; i < 3; ++i) {
        const int a = i;
        const int b = (i + 1) % 3;
        const int64_t dx = tri.x[b] - tri.x[a];
        const int64_t dy = tri.y[b] - tri.y[a];

        step_x[i] = dy;  // dE/dx
        step_y[i] = -dx; // dE/dy
        bias[i] = is_top_left(dx, dy) ? 0 : -1;
        row[i] = edge_at(tri.x[a], tri.y[a], tri.x[b], tri.y[b], min_x, min_y);
    }

    // Barycentric coordinates from the same edge functions - for a
    // point p inside, E_i(p) is twice the area of the sub-triangle opposite
    // vertex (i + 2) % 3, so w_(i+2) = E_i(p) / area2. The three weights are
    // non-negative, sum to 1 (the three sub-areas tile the triangle), and equal
    // (1,0,0) at v0 and so on - exactly the affine interpolation Gouraud
    // shading needs. Reusing the numbers the inside test already computed means
    // vertex-colour interpolation costs no extra geometry work and, since each
    // E is stepped incrementally, NO division per pixel: only the reciprocal of
    // area2, hoisted out of both loops.
    const double inv_area2 = 1.0 / static_cast<double>(area2);

    const bool textured = stage.active();

    // AFFINE texture mapping - uv is interpolated with the very same
    // screen-space barycentrics as the vertex colours, NOT divided through by a
    // per-vertex 1/w. This is not a shortcut a later stage repairs: an rv_vertex
    // carries x, y, colour and uv and no w at all (pdk/cv/rv_vertex.h - the
    // disc hands the console screen positions, not clip-space points), so the
    // information perspective correction needs does not exist on this side of the
    // contract and cannot be reconstructed here. It is inherited from the PSX on
    // purpose, not missed.
    //
    // The visible consequence, and the reason the contract is shaped this way: a
    // polygon receding into the screen has its texture interpolated linearly in
    // SCREEN space rather than along the surface, so the texels swim as it turns
    // and the two halves of a quad visibly disagree along their shared diagonal.
    // Worst on large, steeply angled surfaces (floors, walls); invisible on small
    // or screen-parallel ones. A disc manages it exactly as PSX games did - by
    // subdividing a big surface into more, smaller polygons.
    rv_pcuvwalk uv;
    if (textured) {
        if (stage.stretch()) {
            // STRETCH ignores the vertex uv entirely: the texture is mapped onto
            // the primitive's bounding box, so the gradient is one texture per
            // box and there are no cross terms.
            uv.du_dx = fx_ratio(stage.view->width, max64(stage.box_w, 1));
            uv.dv_dy = fx_ratio(stage.view->height, max64(stage.box_h, 1));
            uv.u = (min_x - stage.box_x) * uv.du_dx;
            uv.v = (min_y - stage.box_y) * uv.dv_dy;
        } else {
            const int64_t u0 = tri.uv[0].u;
            const int64_t u1 = tri.uv[1].u;
            const int64_t u2 = tri.uv[2].u;
            const int64_t v0 = tri.uv[0].v;
            const int64_t v1 = tri.uv[1].v;
            const int64_t v2 = tri.uv[2].v;

            // u(p) = (E1 * u0 + E2 * u1 + E0 * u2) / area2 - the weights above -
            // and every E is affine with the steps already computed, so the two
            // gradients cost one division each for the whole triangle.
            uv.du_dx = fx_ratio(step_x[1] * u0 + step_x[2] * u1 + step_x[0] * u2, area2);
            uv.du_dy = fx_ratio(step_y[1] * u0 + step_y[2] * u1 + step_y[0] * u2, area2);
            uv.dv_dx = fx_ratio(step_x[1] * v0 + step_x[2] * v1 + step_x[0] * v2, area2);
            uv.dv_dy = fx_ratio(step_y[1] * v0 + step_y[2] * v1 + step_y[0] * v2, area2);

            // Anchor at vertex 0, where the weights are exactly (1, 0, 0) and the
            // coordinate is exactly that vertex's uv - no division and no
            // rounding, so the texel the disc authored to sit on a corner is the
            // one texel guaranteed to land on it.
            uv.u = (u0 << RV_UV_FX_SHIFT) + (min_x - tri.x[0]) * uv.du_dx +
                (min_y - tri.y[0]) * uv.du_dy;
            uv.v = (v0 << RV_UV_FX_SHIFT) + (min_x - tri.x[0]) * uv.dv_dx +
                (min_y - tri.y[0]) * uv.dv_dy;
        }
    }

    for (int64_t y = min_y; y <= max_y; ++y) {
        int64_t e0 = row[0];
        int64_t e1 = row[1];
        int64_t e2 = row[2];
        int64_t u_fx = uv.u;
        int64_t v_fx = uv.v;

        for (int64_t x = min_x; x <= max_x; ++x) {
            if ((e0 + bias[0]) >= 0 && (e1 + bias[1]) >= 0 && (e2 + bias[2]) >= 0) {
                if (textured) {
                    // >> on a signed value floors (C++20 onwards), so a
                    // coordinate lands in the same texel on both sides of zero -
                    // no half-texel jump across u == 0 under TILE.
                    const rv_pctexel_sample texel = rv_pctexel::sample(
                        *stage.view, u_fx >> RV_UV_FX_SHIFT, v_fx >> RV_UV_FX_SHIFT, stage.mapping);
                    // A transparent texel writes NOTHING - not colour, not
                    // depth. The Z test is inside emit(), so simply not calling
                    // it is the whole rule (see rv_pctexel.cpp).
                    if (texel.drawn) {
                        emit(fbuf, x, y, rv_pcraster::dither_rgb555(texel.value, x, y), depth,
                            z_enabled);
                    }
                } else {
                    const double w0 = static_cast<double>(e1) * inv_area2; // opposite edge 1
                    const double w1 = static_cast<double>(e2) * inv_area2; // opposite edge 2
                    const double w2 = static_cast<double>(e0) * inv_area2; // opposite edge 0

                    rv_color color;
                    color.r = clamp_channel(w0 * tri.color[0].r + w1 * tri.color[1].r +
                        w2 * tri.color[2].r);
                    color.g = clamp_channel(w0 * tri.color[0].g + w1 * tri.color[1].g +
                        w2 * tri.color[2].g);
                    color.b = clamp_channel(w0 * tri.color[0].b + w1 * tri.color[1].b +
                        w2 * tri.color[2].b);

                    emit(fbuf, x, y, rv_pcraster::pack_rgb555_dithered(color, x, y), depth,
                        z_enabled);
                }
            }

            e0 += step_x[0];
            e1 += step_x[1];
            e2 += step_x[2];
            u_fx += uv.du_dx;
            v_fx += uv.dv_dx;
        }

        row[0] += step_y[0];
        row[1] += step_y[1];
        row[2] += step_y[2];
        uv.u += uv.du_dy;
        uv.v += uv.dv_dy;
    }
}

rv_line make_edge(const rv_vertex &a, const rv_vertex &b)
{
    rv_line line;
    line.vertexes[0] = a;
    line.vertexes[1] = b;
    return line;
}

} // namespace

void rv_pcraster::draw_polygon(rv_pcfbuf &fbuf, const rv_polygon &polygon,
    const rv_pctexview &texture, int32_t depth, bool z_enabled)
{
    if (polygon.vertex_count != 3 && polygon.vertex_count != 4) {
        return; // frame_put already rejected this; nothing sane to draw
    }
    const bool quad = polygon.vertex_count == 4;

    if (polygon.fill_mode == RV_PRIMITIVE_FILL_MODE_WIREFRAME) {
        // The PERIMETER only. For a quad the (2,3) diagonal is an interior edge
        // of the triangulation, and the contract says wireframe draws "only the
        // edges, not the interior" - so the outline follows the PSX quad vertex
        // order 1-2-4-3 rather than the two triangles' edge sets.
        if (quad) {
            draw_line(fbuf, make_edge(polygon.vertexes[0], polygon.vertexes[1]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[1], polygon.vertexes[3]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[3], polygon.vertexes[2]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[2], polygon.vertexes[0]), depth, z_enabled);
        } else {
            draw_line(fbuf, make_edge(polygon.vertexes[0], polygon.vertexes[1]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[1], polygon.vertexes[2]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[2], polygon.vertexes[0]), depth, z_enabled);
        }
        return;
    }

    // FLAT_COLOURED interpolates the vertex colours (equal colours = flat,
    // different = gouraud - the contract needs no separate shading flag);
    // SAMPLE_TEXTURE replaces that with a texel fetch per pixel. An invalid view
    // means the disc named a region it never uploaded into, and the polygon
    // falls back to its vertex colours rather than disappearing.
    //
    // A quad is drawn as the triangles (1,2,3) and (2,3,4) - 0-based (0,1,2) and
    // (1,2,3). The split is contract, not an implementation choice: it decides
    // how colours and uv interpolate across the surface, so vertex ORDER is part
    // of what the disc specifies (PSX rule, kept on purpose).
    static const int RV_TRI_INDICES[2][3] = { { 0, 1, 2 }, { 1, 2, 3 } };
    const int tri_count = quad ? 2 : 1;

    rv_pctexstage stage;
    if (polygon.fill_mode == RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE) {
        stage.view = &texture;
        stage.mapping = polygon.mapping;

        // The STRETCH box is the WHOLE polygon's screen bounding box, computed
        // once here and shared by both triangles of a quad. Letting each
        // triangle stretch over its own box would map the texture twice - once
        // per half - and split the quad down its diagonal.
        int64_t min_x = polygon.vertexes[0].x;
        int64_t min_y = polygon.vertexes[0].y;
        int64_t max_x = min_x;
        int64_t max_y = min_y;
        for (uint32_t i = 1; i < polygon.vertex_count; ++i) {
            min_x = min64(min_x, polygon.vertexes[i].x);
            min_y = min64(min_y, polygon.vertexes[i].y);
            max_x = max64(max_x, polygon.vertexes[i].x);
            max_y = max64(max_y, polygon.vertexes[i].y);
        }
        stage.box_x = min_x;
        stage.box_y = min_y;
        stage.box_w = max_x - min_x + 1;
        stage.box_h = max_y - min_y + 1;
    }

    for (int t = 0; t < tri_count; ++t) {
        rv_pctri tri;
        for (int i = 0; i < 3; ++i) {
            const rv_vertex &vertex = polygon.vertexes[RV_TRI_INDICES[t][i]];
            tri.x[i] = vertex.x;
            tri.y[i] = vertex.y;
            tri.color[i] = vertex.color;
            tri.uv[i] = vertex.uv;
        }
        fill_triangle(fbuf, tri, stage, depth, z_enabled);
    }
}

} // namespace rv_3dmppc
