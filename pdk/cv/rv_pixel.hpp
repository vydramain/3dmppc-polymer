#pragma once

#include <cstdint>

namespace rv_3dmppc {

struct rv_pixel {
//    15bit Direct Display (default) (works with polygons, lines, rectangles)
//    0-4   Red       (0..31)
//    5-9   Green     (0..31)
//    10-14 Blue      (0..31)
//    15    Mask flag (0=Normal, 1=Do not allow to overwrite this pixel)
//    24bit Direct Display (works ONLY with direct vram transfers)
//    0-7    Red      (0..255)
//    8-15   Green    (0..255)
//    16-23  Blue     (0..255)
    int16_t dd_15;
    int32_t dd_24;
};

}  // namespace rv_3dmppc
