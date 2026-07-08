#include "gpu/rasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rv_3dmppc {

namespace {

// Signed area of the triangle (a, b, c) times 2, in screen space.
float edge(float ax, float ay, float bx, float by, float cx, float cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

}  // namespace

void rv_Rasterizer::drawMesh(const Mesh& mesh, const Mat4& mvp, const rv_Texture* tex) {
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        std::array<SVert, 3> sv;
        bool behind = false;

        for (int k = 0; k < 3; ++k) {
            const Vertex& v = mesh.vertices[mesh.indices[i + k]];
            const Vec4 clip = mvp * Vec4{v.pos.x, v.pos.y, v.pos.z, 1.0f};

            // Reject triangles crossing the near plane (no clipping yet).
            if (clip.w <= 1e-6f) {
                behind = true;
                break;
            }

            const float invW = 1.0f / clip.w;
            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;
            const float ndcZ = clip.z * invW;

            sv[k].x = (ndcX * 0.5f + 0.5f) * fb_.width();
            sv[k].y = (1.0f - (ndcY * 0.5f + 0.5f)) * fb_.height();  // flip Y
            sv[k].z = ndcZ;
            sv[k].invW = invW;
            sv[k].uv = v.uv;
            sv[k].color = v.color;
        }

        if (!behind) rasterizeTriangle(sv[0], sv[1], sv[2], tex);
    }
}

void rv_Rasterizer::rasterizeTriangle(const SVert& a, const SVert& b, const SVert& c,
                                   const rv_Texture* tex) {
    const float area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
    if (std::fabs(area) < 1e-6f) return;  // degenerate
    const float invArea = 1.0f / area;

    // Bounding box clamped to the framebuffer.
    int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    int maxX = std::min(fb_.width() - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    int maxY = std::min(fb_.height() - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;

            // Barycentric weights via edge functions.
            float w0 = edge(b.x, b.y, c.x, c.y, px, py) * invArea;
            float w1 = edge(c.x, c.y, a.x, a.y, px, py) * invArea;
            float w2 = edge(a.x, a.y, b.x, b.y, px, py) * invArea;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;  // outside triangle

            // Depth test (smaller = nearer).
            const float z = w0 * a.z + w1 * b.z + w2 * c.z;
            if (z < 0.0f || z > 1.0f) continue;
            if (z >= fb_.depth(x, y)) continue;

            // Perspective-correct interpolation of attributes.
            const float invW = w0 * a.invW + w1 * b.invW + w2 * c.invW;
            const float ww = 1.0f / invW;
            auto persp = [&](float va, float vb, float vc) {
                return (w0 * va * a.invW + w1 * vb * b.invW + w2 * vc * c.invW) * ww;
            };

            Vec3 col{
                persp(a.color.x, b.color.x, c.color.x),
                persp(a.color.y, b.color.y, c.color.y),
                persp(a.color.z, b.color.z, c.color.z),
            };

            if (tex && tex->valid()) {
                const float u = persp(a.uv.x, b.uv.x, c.uv.x);
                const float v = persp(a.uv.y, b.uv.y, c.uv.y);
                const Color t = tex->sample(u, v);
                col.x *= ((t >> 16) & 0xFF) / 255.0f;
                col.y *= ((t >> 8) & 0xFF) / 255.0f;
                col.z *= (t & 0xFF) / 255.0f;
            }

            fb_.setDepth(x, y, z);
            fb_.plot(x, y, fromVec3(col));
        }
    }
}

}  // namespace rv_3dmppc
