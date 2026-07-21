#pragma once

#include "rv_vertex.hpp"

namespace rv_3dmppc {

// An axis-aligned rectangle ("sprite") handed to rv_cv::frame_put() inside an
// rv_primitive. Cheaper to draw than a polygon: no shading and no rotation. Like
// rv_polygon it is self-contained, since the console re-orders primitives.
//
// A rectangle has a single colour; the per-vertex colour / uv fields of its
// corners are unused for now.
//
// DEFERRED: texturing (addresses / mapping) and the blending flags — a rectangle
// is a flat-coloured quad at this stage.
struct rv_rectangle {
    rv_color color;
    rv_vertex vertexes[4];
};

}  // namespace rv_3dmppc
