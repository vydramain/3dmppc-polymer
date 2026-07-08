#include "assets/image.hpp"

#include <cstdio>
#include <vector>

#include <stb_image.h>

namespace rv_3dmppc {

std::optional<rv_Texture> loadImage(const std::string& path) {
    int w = 0, h = 0, channels = 0;
    // Force 4 channels (RGBA) so the layout is predictable.
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::fprintf(stderr, "loadImage: %s (%s)\n", stbi_failure_reason(), path.c_str());
        return std::nullopt;
    }

    std::vector<Color> texels(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < texels.size(); ++i) {
        const unsigned char* p = data + i * 4;
        texels[i] = rgba(p[0], p[1], p[2], p[3]);
    }
    stbi_image_free(data);

    return rv_Texture(w, h, std::move(texels));
}

}  // namespace rv_3dmppc
