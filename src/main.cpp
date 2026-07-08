// 3dmppc demo: a rotating, textured cube drawn by the software rasterizer at the
// console's native 320x240. Everything here is deliberately explicit — this is
// the seed of a PSX-like fantasy console runtime.
#include <SDL3/SDL.h>

#include <cstdio>

#include "assets/image.hpp"
#include "assets/obj_loader.hpp"
#include "core/window.hpp"
#include "gpu/mesh.hpp"
#include "gpu/rasterizer.hpp"
#include "math/math.hpp"

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kScale = 3;

// Fallback unit cube, used when assets/cube.obj is missing.
rv_3dmppc::Mesh makeCube() {
    using rv_3dmppc::Vertex;
    rv_3dmppc::Mesh m;
    const rv_3dmppc::Vec3 p[8] = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
    };
    const rv_3dmppc::Vec2 uv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    auto quad = [&](int a, int b, int c, int d) {
        const int corner[4] = {a, b, c, d};
        std::uint32_t base = static_cast<std::uint32_t>(m.vertices.size());
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.pos = p[corner[i]];
            v.uv = uv[i];
            v.color = {p[corner[i]].x * 0.5f + 0.5f, p[corner[i]].y * 0.5f + 0.5f,
                       p[corner[i]].z * 0.5f + 0.5f};
            m.vertices.push_back(v);
        }
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + i);
    };
    quad(0, 1, 2, 3);  // back
    quad(5, 4, 7, 6);  // front
    quad(4, 0, 3, 7);  // left
    quad(1, 5, 6, 2);  // right
    quad(3, 2, 6, 7);  // top
    quad(4, 5, 1, 0);  // bottom
    return m;
}

}  // namespace

int main(int, char**) {
    rv_3dmppc::rv_Window window("3dmppc — software rasterizer", kWidth, kHeight, kScale);
    if (!window.ok()) return 1;

    rv_3dmppc::rv_Framebuffer fb(kWidth, kHeight);
    rv_3dmppc::rv_Rasterizer raster(fb);

    // Geometry: prefer the asset, fall back to a generated cube.
    rv_3dmppc::Mesh mesh = rv_3dmppc::loadObj("assets/cube.obj").value_or(makeCube());

    // rv_Texture: prefer a PNG, fall back to a procedural checker.
    rv_3dmppc::rv_Texture texture =
        rv_3dmppc::loadImage("assets/texture.png")
            .value_or(rv_3dmppc::rv_Texture::checker(64, rv_3dmppc::rgb(230, 230, 230),
                                            rv_3dmppc::rgb(90, 90, 90)));

    const rv_3dmppc::Mat4 proj = rv_3dmppc::perspectiveLH(
        60.0f * 3.14159265f / 180.0f, float(kWidth) / kHeight, 0.1f, 100.0f);
    const rv_3dmppc::Mat4 view =
        rv_3dmppc::lookAtLH({0, 1.2f, -3.2f}, {0, 0, 0}, {0, 1, 0});

    std::uint64_t prev = SDL_GetTicks();
    float angle = 0.0f;

    while (window.pumpEvents()) {
        const std::uint64_t now = SDL_GetTicks();
        const float dt = (now - prev) / 1000.0f;
        prev = now;
        angle += dt;

        const rv_3dmppc::Mat4 model = rv_3dmppc::rotationY(angle) * rv_3dmppc::rotationX(0.5f);
        const rv_3dmppc::Mat4 mvp = proj * view * model;

        fb.clear(rv_3dmppc::rgb(24, 22, 40));  // deep PSX blue-grey
        raster.drawMesh(mesh, mvp, &texture);
        window.present(fb);
    }

    return 0;
}
