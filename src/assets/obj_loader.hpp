// Minimal Wavefront .obj loader. Handles v / vt / vn and polygon faces with
// the forms `f a`, `f a/b`, `f a//c`, `f a/b/c`; polygons are fan-triangulated.
// Intentionally small and dependency-free — swap in tinyobjloader here later if
// you need materials, smoothing groups, etc.
#pragma once

#include <optional>
#include <string>

#include "gpu/mesh.hpp"

namespace rv_3dmppc {

// Returns std::nullopt if the file cannot be opened.
std::optional<Mesh> loadObj(const std::string& path);

}  // namespace rv_3dmppc
