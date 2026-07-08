// SDL3 window that presents the CPU framebuffer, scaled up with nearest-neighbour
// integer scaling to keep the chunky PSX look.
#pragma once

#include <string>

#include "core/framebuffer.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace rv_3dmppc {

class rv_Window {
public:
    // logicalW/H is the native console resolution; scale is the initial window
    // magnification (window = logical * scale).
    rv_Window(const std::string& title, int logicalW, int logicalH, int scale);
    ~rv_Window();

    rv_Window(const rv_Window&) = delete;
    rv_Window& operator=(const rv_Window&) = delete;

    bool ok() const { return renderer_ != nullptr; }

    // Pump events; returns false when the user asked to quit (window close / Esc).
    bool pumpEvents();

    // Upload the framebuffer and present it.
    void present(const rv_Framebuffer& fb);

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int logicalW_, logicalH_;
    bool running_ = true;
};

}  // namespace rv_3dmppc
