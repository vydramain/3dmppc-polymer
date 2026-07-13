#include "core/window.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

namespace rv_3dmppc {

rv_Window::rv_Window(const std::string& title, int logicalW, int logicalH, int scale)
    : logicalW_(logicalW), logicalH_(logicalH) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return;
    }

    window_ = SDL_CreateWindow(title.c_str(), logicalW * scale, logicalH * scale,
                               SDL_WINDOW_RESIZABLE);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return;
    }

    // Present the framebuffer at logical size with crisp integer scaling.
    SDL_SetRenderLogicalPresentation(renderer_, logicalW, logicalH,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, logicalW, logicalH);
    if (!texture_) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);

    // Grab the mouse for first-person look. Esc frees it, a click re-grabs.
    setMouseGrab(true);
}

void rv_Window::setMouseGrab(bool grab) {
    if (!window_ || grab == mouseGrabbed_) return;
    if (SDL_SetWindowRelativeMouseMode(window_, grab)) {
        mouseGrabbed_ = grab;
        mouseDx_ = mouseDy_ = 0.0f;  // drop the jump across a grab transition
    }
}

void rv_Window::consumeMouseDelta(float& dx, float& dy) {
    dx = mouseDx_;
    dy = mouseDy_;
    mouseDx_ = mouseDy_ = 0.0f;
}

rv_Window::~rv_Window() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool rv_Window::pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            // Esc releases a grabbed cursor first; press it again to quit.
            if (e.key.key == SDLK_ESCAPE) {
                if (mouseGrabbed_) setMouseGrab(false);
                else running_ = false;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!mouseGrabbed_) setMouseGrab(true);  // click to re-capture
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (mouseGrabbed_) {
                mouseDx_ += e.motion.xrel;
                mouseDy_ += e.motion.yrel;
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            setMouseGrab(false);  // don't trap the cursor on alt-tab
            break;
        default:
            break;
        }
    }
    return running_;
}

void rv_Window::present(const rv_Framebuffer& fb) {
    SDL_UpdateTexture(texture_, nullptr, fb.pixels(), fb.pitch());
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

}  // namespace rv_3dmppc
