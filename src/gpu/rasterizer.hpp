// Triangle-list software rasterizer with a z-buffer and perspective-correct
// attribute interpolation. Deliberately tiny: the starting point for a
// PSX-like GPU emulation.
#pragma once

#include "core/framebuffer.hpp"
#include "gpu/mesh.hpp"
#include "gpu/texture.hpp"
#include "math/math.hpp"

namespace rv_3dmppc {

class rv_Rasterizer {
public:
    explicit rv_Rasterizer(rv_Framebuffer& fb) : fb_(fb) {}

    // Draw a mesh transformed by `mvp`. If `tex` is non-null and valid, texels
    // modulate the interpolated vertex color; otherwise vertex color is used.
    void drawMesh(const Mesh& mesh, const Mat4& mvp, const rv_Texture* tex = nullptr);

private:
    // Per-vertex data after projection, in screen space.
    struct SVert {
        float x, y, z;  // screen x/y (pixels), ndc depth [0,1]
        float invW;     // 1/clip.w for perspective-correct interpolation
        Vec2 uv;
        Vec3 color;
    };

    void rasterizeTriangle(const SVert& a, const SVert& b, const SVert& c,
                           const rv_Texture* tex);

    rv_Framebuffer& fb_;
};

}  // namespace rv_3dmppc
