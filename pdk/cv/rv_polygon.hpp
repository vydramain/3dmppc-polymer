#pragma once

#include <cstdint>

namespace rv_3dmppc {

  struct rv_clut {
//    16bit Texture (Direct Color)             ;(One 256x256 page = 128Kbytes)
//    0-4   Red       (0..31)         ;\Color 0000h        = Fully-transparent
//    5-9   Green     (0..31)         ; Color 0001h..7FFFh = Non-transparent
//    10-14 Blue      (0..31)         ; Color 8000h..FFFFh = Semi-transparent (*)
//    15    Semi-transparency Flag    ;/(*) or Non-transparent for opaque commands
//    8bit Texture (256 Color Palette)         ;(One 256x256 page = 64Kbytes)
//    0-7   Palette index for 1st pixel (left)
//    8-15  Palette index for 2nd pixel (right)
//    4bit Texture (16 Color Palette)          ;(One 256x256 page = 32Kbytes)
//    0-3   Palette index for 1st pixel (left)
//    4-7   Palette index for 2nd pixel (middle/left)
//    8-11  Palette index for 3rd pixel (middle/right)
//    12-15 Palette index for 4th pixel (right)
  };

struct rv_uv {
    uint32_t clut_vvuu, page_vvuu;
};

struct rv_polygon {
    uint32_t color, vertex;
    rv_uv uv;
};

}  // namespace rv_3dmppc
