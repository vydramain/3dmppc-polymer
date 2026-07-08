// Paletted-era style texture: 32-bit RGBA source, nearest sampling, no filtering
// (per docs/specs.md — "textures без фильтрации").
#pragma once

#include <cstdint>
#include <vector>

#include "core/color.hpp"

namespace rv_3dmppc {

class rv_Texture {
public:
    rv_Texture() = default;
    rv_Texture(int width, int height, std::vector<Color> texels)
        : width_(width), height_(height), texels_(std::move(texels)) {}

    bool valid() const { return width_ > 0 && height_ > 0; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Nearest-neighbour sample with wrap. u,v in [0,1].
    Color sample(float u, float v) const {
        int x = static_cast<int>(u * width_) % width_;
        int y = static_cast<int>(v * height_) % height_;
        if (x < 0) x += width_;
        if (y < 0) y += height_;
        return texels_[static_cast<std::size_t>(y) * width_ + x];
    }

    // Handy fallback when no PNG is available.
    static rv_Texture checker(int size, Color a, Color b, int cells = 8) {
        std::vector<Color> t(static_cast<std::size_t>(size) * size);
        const int cell = size / cells;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                t[static_cast<std::size_t>(y) * size + x] =
                    ((x / cell + y / cell) & 1) ? a : b;
        return rv_Texture(size, size, std::move(t));
    }

private:
    int width_ = 0, height_ = 0;
    std::vector<Color> texels_;
};

}  // namespace rv_3dmppc
