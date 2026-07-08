// CPU color + depth buffer at the console's native resolution.
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "core/color.hpp"

namespace rv_3dmppc {

class rv_Framebuffer {
public:
    rv_Framebuffer(int width, int height)
        : width_(width),
          height_(height),
          color_(static_cast<std::size_t>(width) * height, 0),
          depth_(static_cast<std::size_t>(width) * height, 0.0f) {}

    int width() const { return width_; }
    int height() const { return height_; }

    void clear(Color c) {
        std::fill(color_.begin(), color_.end(), c);
        std::fill(depth_.begin(), depth_.end(), std::numeric_limits<float>::infinity());
    }

    // No bounds check: callers rasterize inside the viewport bounds.
    void plot(int x, int y, Color c) { color_[index(x, y)] = c; }

    float depth(int x, int y) const { return depth_[index(x, y)]; }
    void setDepth(int x, int y, float z) { depth_[index(x, y)] = z; }

    const Color* pixels() const { return color_.data(); }
    int pitch() const { return width_ * static_cast<int>(sizeof(Color)); }

private:
    std::size_t index(int x, int y) const {
        return static_cast<std::size_t>(y) * width_ + x;
    }

    int width_, height_;
    std::vector<Color> color_;
    std::vector<float> depth_;
};

}  // namespace rv_3dmppc
