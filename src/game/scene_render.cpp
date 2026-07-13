// Scene renderer (Agent E): turns a frame's DrawList into rasterized pixels.
// Gameplay only ever pushes DrawInstances; here we resolve each MeshId to its
// procedural mesh + texture (owned by the registry) and draw it with a
// per-instance model transform and tint.
#include "game/scene_render.hpp"

namespace rv_3dmppc {

void SceneRenderer::init() { registry_.build(); }

void SceneRenderer::draw(const DrawList& list, const Camera& cam, rv_Framebuffer& fb,
                         Color clearColor) {
    // Clear to the area's atmosphere colour (color + depth).
    fb.clear(clearColor);

    // One rasterizer over the target framebuffer for the whole pass.
    rv_Rasterizer raster(fb);

    // view * projection, shared across every instance this frame.
    const Mat4 vp = cam.viewProj();

    for (const DrawInstance& inst : list) {
        // Skip the null handle and anything the registry couldn't build.
        if (inst.mesh == MeshId::None || inst.mesh == MeshId::Count) continue;
        const Mesh& mesh = registry_.mesh(inst.mesh);
        if (mesh.vertices.empty() || mesh.indices.size() < 3) continue;

        // model = T * Ry * Rx * Rz * S  (yaw, then pitch, then roll, then scale).
        const Mat4 model = translation(inst.pos) *
                           rotationY(inst.yaw) *
                           rotationX(inst.pitch) *
                           rotationZ(inst.roll) *
                           scaling(inst.scale);
        const Mat4 mvp = vp * model;

        // The z-buffer resolves overlap, so draw order is free; SmokeCloud is
        // just a normal mesh with a pale tint (no real alpha for the template).
        raster.drawMesh(mesh, mvp, registry_.texture(inst.mesh), inst.tint);
    }
}

}  // namespace rv_3dmppc
