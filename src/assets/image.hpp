// PNG (and other formats) loading via stb_image, decoded straight into a rv_Texture.
#pragma once

#include <optional>
#include <string>

#include "gpu/texture.hpp"

namespace rv_3dmppc {

// Returns std::nullopt if the file cannot be loaded/decoded.
std::optional<rv_Texture> loadImage(const std::string& path);

}  // namespace rv_3dmppc
