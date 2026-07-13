// Procedural asset library: builds every MeshId's low-poly mesh + paletted
// texture at boot, under console budget (16-bit-friendly palettes, small
// nearest-sampled atlases). No external files required — the whole disc is
// self-generating. OWNED BY AGENT E.
#pragma once

#include <array>

#include "types.hpp"
#include "gpu/mesh.hpp"
#include "gpu/texture.hpp"

namespace rv_3dmppc {

// Maps MeshId → concrete mesh + texture. Built once at boot.
class MeshRegistry {
public:
    // Procedurally construct all meshes/textures. Also attempts to load the
    // disc's real protagonist model for MeshId::Protagonist, cube-fallback.
    void build();

    const Mesh& mesh(MeshId id) const { return meshes_[static_cast<int>(id)]; }
    const rv_Texture* texture(MeshId id) const {
        const rv_Texture& t = textures_[static_cast<int>(id)];
        return t.valid() ? &t : nullptr;
    }

private:
    std::array<Mesh, static_cast<int>(MeshId::Count)> meshes_{};
    std::array<rv_Texture, static_cast<int>(MeshId::Count)> textures_{};
};

}  // namespace rv_3dmppc
