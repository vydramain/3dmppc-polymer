// Triangle mesh consumed by the rasterizer.
#pragma once

#include <cstdint>
#include <vector>

#include "math/math.hpp"

namespace rv_3dmppc {

struct Vertex {
    Vec3 pos;
    Vec3 normal{0, 0, 1};
    Vec2 uv;
    Vec3 color{1, 1, 1};
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;  // triangle list (multiple of 3)
};

}  // namespace rv_3dmppc
