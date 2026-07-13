// Scene renderer: consumes a DrawList + Camera and draws it through the software
// rasterizer, then the world is complete for the frame. Owns the mesh registry.
// OWNED BY AGENT E.
//
// This is the only game code that touches the rasterizer/framebuffer for 3D;
// gameplay systems just push DrawInstances. Applies per-instance transform +
// tint, sky/atmosphere clear, and any cheap palette/dither mood pass.
#pragma once

#include "core/framebuffer.hpp"
#include "game/assets_gen.hpp"
#include "game/types.hpp"
#include "gpu/camera.hpp"
#include "gpu/rasterizer.hpp"

namespace rv_3dmppc {

class SceneRenderer {
public:
    void init();  // builds the mesh registry

    // Clear to the area's atmosphere color, then draw every instance.
    void draw(const DrawList& list, const Camera& cam, rv_Framebuffer& fb,
              Color clearColor);

    const MeshRegistry& registry() const { return registry_; }

private:
    MeshRegistry registry_;
};

}  // namespace rv_3dmppc
