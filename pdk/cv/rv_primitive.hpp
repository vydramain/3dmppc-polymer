#pragma once

#include <cstdint>

#include "rv_polygon.hpp"
#include "rv_rectangle.hpp"

namespace rv_3dmppc {

// Which member of rv_primitive::data is active.
enum rv_primitive_type {
    RV_PRIMITIVE_POLYGON = 1,
    RV_PRIMITIVE_RECTANGLE = 2,
};

// One thing to draw this frame, handed to rv_cv::frame_put(). The console does not
// draw it immediately: it files every primitive by `depth` and, at frame_flush(),
// renders them far-to-near (the ordering table). So `depth` is a sort key over the
// whole primitive, not a per-pixel Z — a whole primitive goes in front of or
// behind another.
//
// `depth` is signed and lives on the primitive itself (shared by every variant)
// because polygons and rectangles are ordered together. Its range and direction,
// and how ties and out-of-range values behave, are the console's contract: a
// larger value is farther, ties keep submission order, out-of-range clamps.
struct rv_primitive {
    uint32_t type;   // rv_primitive_type: selects the union member
    int32_t depth;   // ordering-table sort key (larger = farther)

    union {
        rv_polygon polygon;
        rv_rectangle rectangle;
    } data;
};

}  // namespace rv_3dmppc
