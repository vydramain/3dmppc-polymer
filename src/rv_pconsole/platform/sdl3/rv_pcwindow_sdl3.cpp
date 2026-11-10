// The SDL3 window: creation, the renderer/streaming-texture pair a finished
// frame lands in, and the keyboard/mouse overlay that only exists while a
// window does.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include <cmath>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace rv_3dmppc
{

namespace
{

// One ARGB8888 pixel is 4 bytes; the SDL texture pitch is the screen width times this.
constexpr int ARGB8888_BYTES_PER_PIXEL = 4;

// What the keyboard overlay on port 0 is able to report. A keyboard has no
// analog anything, but the layout below drives the sticks digitally, so the
// stick sources are advertised honestly - a disc that asks "can port 0 give me
// a left stick?" gets yes, and gets +-1 values.
constexpr uint64_t RV_PCWINDOW_SDL3_KEYBOARD_ABILITIES = RV_ISOURCE_FRONT_BTTN_SOUTH | RV_ISOURCE_FRONT_BTTN_EAST |
    RV_ISOURCE_FRONT_BTTN_WEST | RV_ISOURCE_FRONT_BTTN_NORTH | RV_ISOURCE_BUMPER_LEFT | RV_ISOURCE_BUMPER_RIGHT |
    RV_ISOURCE_MENU_BTTN_MENU | RV_ISOURCE_MENU_BTTN_VIEW | RV_ISOURCE_DPAD_MOVE | RV_ISOURCE_DPAD_NORTH |
    RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_EAST | RV_ISOURCE_LEFT_STICK_MOVE |
    RV_ISOURCE_LEFT_STICK_DPAD_NORTH | RV_ISOURCE_LEFT_STICK_DPAD_SOUTH | RV_ISOURCE_LEFT_STICK_DPAD_WEST |
    RV_ISOURCE_LEFT_STICK_DPAD_EAST | RV_ISOURCE_RIGHT_STICK_MOVE | RV_ISOURCE_RIGHT_STICK_DPAD_NORTH |
    RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH | RV_ISOURCE_RIGHT_STICK_DPAD_WEST | RV_ISOURCE_RIGHT_STICK_DPAD_EAST;

// sectorization. An analog stick becomes four digital directions by
// comparing |x| against |y|: the dominant axis picks the sector, its sign
// picks the side. This is the diagonal-free 4-way reading; a disc that wants
// the diagonals reads the raw axes instead.
uint64_t stick_direction_bits(rv_iaxes axes, uint64_t north, uint64_t south, uint64_t west, uint64_t east, uint64_t move)
{
    if (axes.x == 0.0f && axes.y == 0.0f) {
        return 0;
    }

    uint64_t bits = move;
    if (std::fabs(axes.x) > std::fabs(axes.y)) {
        bits |= axes.x > 0.0f ? east : west;
    } else {
        bits |= axes.y > 0.0f ? north : south;
    }
    return bits;
}

// The largest frame-shaped rectangle inside a buffer, in buffer pixels. The scale is
// fractional on purpose: the window fills whatever size it got. Centred across; down
// the middle when the buffer is not taller than wide, pinned to the top otherwise.
SDL_FRect frame_rect_for(int buffer_w, int buffer_h, int64_t frame_w, int64_t frame_h)
{
    const float fit_x = static_cast<float>(buffer_w) / static_cast<float>(frame_w);
    const float fit_y = static_cast<float>(buffer_h) / static_cast<float>(frame_h);
    const float fit = std::fmin(fit_x, fit_y);

    const float w = static_cast<float>(frame_w) * fit;
    const float h = static_cast<float>(frame_h) * fit;
    const float x = (static_cast<float>(buffer_w) - w) / 2.0f;
    const float y = buffer_h > buffer_w ? 0.0f : (static_cast<float>(buffer_h) - h) / 2.0f;
    return SDL_FRect{ x, y, w, h };
}

// Web only. Without SDL_WINDOW_OPENGL and this GLES version SDL recreates the window: Emscripten has no reconfigure hook, so
// SDL_ReconfigureWindow recreates it, and at devicePixelRatio != 1 the old window leaves the canvas CSS at 1x1; the
// new window reads that as no external size and writes its own (960x720 at dpr 2). Keep equal to the renderer.
#if defined(__EMSCRIPTEN__)
constexpr int RV_PCWINDOW_SDL3_GLES_MAJOR = 3;
constexpr int RV_PCWINDOW_SDL3_GLES_MINOR = 0;
constexpr SDL_WindowFlags RV_PCWINDOW_SDL3_GL_FLAGS = SDL_WINDOW_OPENGL;
#else
constexpr SDL_WindowFlags RV_PCWINDOW_SDL3_GL_FLAGS = 0;
#endif

// Web only: sets the GLES context request before the window exists. Native keeps SDL's defaults.
int request_gles_context()
{
#if defined(__EMSCRIPTEN__)
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, RV_PCWINDOW_SDL3_GLES_MAJOR) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, RV_PCWINDOW_SDL3_GLES_MINOR)) {
        RV_LOG_ERR("pcplatform", "SDL_GL_SetAttribute failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
#endif
    return RV_OK;
}

} // namespace

rv_pcwindow_sdl3::~rv_pcwindow_sdl3()
{
    // Texture before renderer before window: destroying the renderer first
    // would leave the texture handle dangling, and the renderer needs the
    // window to still exist.
    if (texture_) {
        SDL_DestroyTexture(texture_);
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
    }
    if (window_) {
        SDL_DestroyWindow(window_);
    }
}

int64_t rv_pcwindow_sdl3::open(const char *title, int64_t screen_width, int64_t screen_height, uint64_t scale)
{
    // null object. cv=null means no window was ever wanted, and that
    // is a successful no-op, not a failure - RV_ERR_IO is reserved for a
    // window that WAS wanted and could not be made.
    if (!wanted_) {
        return RV_OK;
    }

    if (!video_ready_) {
        RV_LOG_ERR("pcplatform", "video never came up, refusing to open a window");
        return RV_ERR_IO;
    }

    if (scale == 0) {
        scale = 1;
    }

    // The window is made once. A second call with the same geometry only retitles it; another geometry is refused.
    const bool made = window_ != nullptr;
    const bool same_geometry = screen_width == screen_width_ && screen_height == screen_height_ && scale == scale_;
    if (made && !same_geometry) {
        RV_LOG_ERR("pcplatform",
            "window is {}x{} at scale {}, refusing {}x{} at scale {}",
            screen_width_,
            screen_height_,
            scale_,
            screen_width,
            screen_height,
            scale);
        return RV_ERR_INVAL;
    }
    if (made) {
        if (!SDL_SetWindowTitle(window_, title)) {
            RV_LOG_WARN("pcplatform", "SDL_SetWindowTitle failed: {}", SDL_GetError());
        }
        return RV_OK;
    }

    const int window_w = static_cast<int>(screen_width * static_cast<int64_t>(scale));
    const int window_h = static_cast<int>(screen_height * static_cast<int64_t>(scale));
    // High pixel density makes the buffer physical pixels, so the frame stays sharp on HiDPI screens.
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | RV_PCWINDOW_SDL3_GL_FLAGS;
    if (request_gles_context() != RV_OK) {
        return RV_ERR_IO;
    }
    window_ = SDL_CreateWindow(title, window_w, window_h, flags);
    if (!window_) {
        RV_LOG_ERR("pcplatform", "SDL_CreateWindow failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        RV_LOG_ERR("pcplatform", "SDL_CreateRenderer failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    // No logical presentation: present() fits the frame into the physical buffer at a fractional scale.
    // PIXELART keeps each source pixel a solid block; only the block edges blend, into one screen row or column.
    texture_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(screen_width),
        static_cast<int>(screen_height));
    if (!texture_) {
        RV_LOG_ERR("pcplatform", "SDL_CreateTexture failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_PIXELART);

    screen_width_ = screen_width;
    screen_height_ = screen_height;
    scale_ = scale;

    RV_LOG_INFO("pcplatform", "window {}x{} at scale {}", screen_width, screen_height, scale);
    return RV_OK;
}

bool rv_pcwindow_sdl3::presenting() const
{
    return renderer_ && texture_;
}

void rv_pcwindow_sdl3::present(const uint32_t *argb)
{
    if (!argb || !renderer_ || !texture_) {
        return;
    }
    if (update_frame_rect() != RV_OK) {
        return;
    }
#if defined(__EMSCRIPTEN__)
    report_frame_layout();
#endif

    SDL_UpdateTexture(texture_, nullptr, argb, static_cast<int>(screen_width_ * ARGB8888_BYTES_PER_PIXEL));
    // The area outside the frame is the letterbox: clear it to opaque black.
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, &frame_rect_);
    SDL_RenderPresent(renderer_);
}

int rv_pcwindow_sdl3::update_frame_rect()
{
    // frame_rect_for() divides by the frame size; a frame without size has no rectangle.
    if (screen_width_ <= 0 || screen_height_ <= 0) {
        RV_LOG_ERR("pcplatform", "frame is {}x{}, no rectangle to fit", screen_width_, screen_height_);
        return RV_ERR_INVAL;
    }

    int buffer_w = 0;
    int buffer_h = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer_, &buffer_w, &buffer_h)) {
        RV_LOG_ERR("pcplatform", "SDL_GetCurrentRenderOutputSize failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    buffer_w_ = buffer_w;
    buffer_h_ = buffer_h;
    frame_rect_ = frame_rect_for(buffer_w, buffer_h, screen_width_, screen_height_);
    return RV_OK;
}

#if defined(__EMSCRIPTEN__)
// Web only: tells the page the frame rectangle, the buffer size and the frame width in console pixels, so its on-screen
// gamepad lays out around the frame. Sent only when one of them changed since the last delivery. A page without the hook
// answers 0 and nothing is recorded, so the next present() tries again.
void rv_pcwindow_sdl3::report_frame_layout()
{
    const int frame_w = static_cast<int>(screen_width_);
    const bool unchanged = frame_rect_.x == layout_rect_.x && frame_rect_.y == layout_rect_.y &&
        frame_rect_.w == layout_rect_.w && frame_rect_.h == layout_rect_.h && buffer_w_ == layout_out_w_ &&
        buffer_h_ == layout_out_h_ && frame_w == layout_frame_w_;
    if (layout_sent_ && unchanged) {
        return;
    }

    const int delivered = EM_ASM_INT(
        {
            if (typeof Module.rvTouchpadLayout != 'function') {
                return 0;
            }
            Module.rvTouchpadLayout($0, $1, $2, $3, $4, $5, $6);
            return 1;
        },
        static_cast<double>(frame_rect_.x),
        static_cast<double>(frame_rect_.y),
        static_cast<double>(frame_rect_.w),
        static_cast<double>(frame_rect_.h),
        buffer_w_,
        buffer_h_,
        frame_w);
    if (delivered == 0) {
        return;
    }

    layout_sent_ = true;
    layout_rect_ = frame_rect_;
    layout_out_w_ = buffer_w_;
    layout_out_h_ = buffer_h_;
    layout_frame_w_ = frame_w;
}
#endif

bool rv_pcwindow_sdl3::close_requested() const
{
    return close_requested_;
}

uint64_t rv_pcwindow_sdl3::keyboard_abilities() const
{
    return window_ ? RV_PCWINDOW_SDL3_KEYBOARD_ABILITIES : 0;
}

rv_istate rv_pcwindow_sdl3::keyboard_state() const
{
    return keyboard_state_;
}

rv_imouse rv_pcwindow_sdl3::consume_mouse()
{
    const rv_imouse motion{ static_cast<int>(mouse_dx_), static_cast<int>(mouse_dy_) };
    mouse_dx_ = 0.0f;
    mouse_dy_ = 0.0f;
    return motion;
}

uint32_t rv_pcwindow_sdl3::consume_pause_requests()
{
    const uint32_t requests = pause_requests_;
    pause_requests_ = 0;
    return requests;
}

rv_pcpresses rv_pcwindow_sdl3::consume_presses()
{
    const rv_pcpresses presses = presses_;
    presses_ = {};
    return presses;
}

rv_pcpointer rv_pcwindow_sdl3::cursor() const
{
    return cursor_;
}

void rv_pcwindow_sdl3::note_close()
{
    close_requested_ = true;
}

void rv_pcwindow_sdl3::note_pause_request()
{
    ++pause_requests_;
}

void rv_pcwindow_sdl3::add_mouse(float dx, float dy)
{
    // rv_cio::imouse is variant B - motion relative to the previous poll - so
    // the deltas accumulate here and are drained on read.
    mouse_dx_ += dx;
    mouse_dy_ += dy;
}

void rv_pcwindow_sdl3::note_key_press(bool escape)
{
    ++presses_.keys;
    if (escape) {
        ++presses_.escape;
    }
}

void rv_pcwindow_sdl3::note_mouse_press(uint8_t button, float x, float y)
{
    ++presses_.mouse_buttons;
    // A click with no presenting window, or with a failed rectangle update, stays outside the frame.
    if (button == SDL_BUTTON_LEFT && presenting() && update_frame_rect() == RV_OK) {
        presses_.last_click = to_frame(x, y);
    }
}

void rv_pcwindow_sdl3::note_cursor(float x, float y)
{
    cursor_x_ = x;
    cursor_y_ = y;
    cursor_on_window_ = true;
}

void rv_pcwindow_sdl3::note_cursor_left()
{
    cursor_on_window_ = false;
}

void rv_pcwindow_sdl3::snapshot_cursor()
{
    // Refresh the frame rectangle first; if it cannot be read, the pointer is reported outside.
    cursor_ = {};
    if (cursor_on_window_ && presenting() && update_frame_rect() == RV_OK) {
        cursor_ = to_frame(cursor_x_, cursor_y_);
    }
}

rv_pcpointer rv_pcwindow_sdl3::to_frame(float x, float y)
{
    // renderer_ is null when no window was wanted (open() is silent then) or when the window, the renderer
    // or video could not be made (open() logged it); either way there is no frame to be inside.
    rv_pcpointer point{};
    if (!renderer_) {
        return point;
    }

    // With logical presentation off this gives buffer pixels (window points times the pixel density).
    float buffer_x = 0.0f;
    float buffer_y = 0.0f;
    if (!SDL_RenderCoordinatesFromWindow(renderer_, x, y, &buffer_x, &buffer_y)) {
        if (!coordinates_warned_) {
            coordinates_warned_ = true;
            RV_LOG_WARN("pcplatform", "SDL_RenderCoordinatesFromWindow failed: {}", SDL_GetError());
        }
        return point;
    }
    // An empty buffer (a minimised window) has no frame to be inside; NaN fails this test too.
    if (!(frame_rect_.w > 0.0f && frame_rect_.h > 0.0f)) {
        return point;
    }

    // Outside the rectangle the frame coordinates fall outside 0..size and inside stays false.
    const float frame_x = (buffer_x - frame_rect_.x) * static_cast<float>(screen_width_) / frame_rect_.w;
    const float frame_y = (buffer_y - frame_rect_.y) * static_cast<float>(screen_height_) / frame_rect_.h;
    point.x = static_cast<int64_t>(std::floor(frame_x));
    point.y = static_cast<int64_t>(std::floor(frame_y));
    point.inside = point.x >= 0 && point.x < screen_width_ && point.y >= 0 && point.y < screen_height_;
    return point;
}

void rv_pcwindow_sdl3::snapshot_keyboard()
{
    keyboard_state_ = rv_istate{};

    if (!window_) {
        return;
    }

    const bool *keys = SDL_GetKeyboardState(nullptr);
    if (!keys) {
        return;
    }

    rv_istate &state = keyboard_state_;

    const struct {
        SDL_Scancode key;
        uint64_t source;
    } key_map[] = {
        // Esc is the console's "Option": the same bit SDL_GAMEPAD_BUTTON_START
        // raises, so the skeleton disc needs no keyboard-specific branch.
        { SDL_SCANCODE_ESCAPE, RV_ISOURCE_MENU_BTTN_MENU },
        { SDL_SCANCODE_TAB, RV_ISOURCE_MENU_BTTN_VIEW },
        { SDL_SCANCODE_SPACE, RV_ISOURCE_FRONT_BTTN_SOUTH },
        { SDL_SCANCODE_Z, RV_ISOURCE_FRONT_BTTN_SOUTH },
        { SDL_SCANCODE_X, RV_ISOURCE_FRONT_BTTN_EAST },
        { SDL_SCANCODE_C, RV_ISOURCE_FRONT_BTTN_WEST },
        { SDL_SCANCODE_V, RV_ISOURCE_FRONT_BTTN_NORTH },
        { SDL_SCANCODE_Q, RV_ISOURCE_BUMPER_LEFT },
        { SDL_SCANCODE_E, RV_ISOURCE_BUMPER_RIGHT },
        { SDL_SCANCODE_UP, RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_MOVE },
        { SDL_SCANCODE_DOWN, RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_MOVE },
        { SDL_SCANCODE_LEFT, RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_MOVE },
        { SDL_SCANCODE_RIGHT, RV_ISOURCE_DPAD_EAST | RV_ISOURCE_DPAD_MOVE },
    };
    for (const auto &entry : key_map) {
        if (keys[entry.key]) {
            state.buttons |= entry.source;
        }
    }

    // WASD drives the left stick, IJKL the right one. Digital keys can only
    // produce the corners of the square, so the values are +-1 with no dead
    // zone to apply - the stick helper would only rescale a magnitude that is
    // already saturated.
    const float wasd_x = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    const float wasd_y = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    const float ijkl_x = (keys[SDL_SCANCODE_L] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_J] ? 1.0f : 0.0f);
    const float ijkl_y = (keys[SDL_SCANCODE_I] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_K] ? 1.0f : 0.0f);

    if (wasd_x != 0.0f || wasd_y != 0.0f) {
        state.left_stick = { wasd_x, wasd_y };
        state.buttons |= stick_direction_bits(state.left_stick,
            RV_ISOURCE_LEFT_STICK_DPAD_NORTH,
            RV_ISOURCE_LEFT_STICK_DPAD_SOUTH,
            RV_ISOURCE_LEFT_STICK_DPAD_WEST,
            RV_ISOURCE_LEFT_STICK_DPAD_EAST,
            RV_ISOURCE_LEFT_STICK_MOVE);
    }
    if (ijkl_x != 0.0f || ijkl_y != 0.0f) {
        state.right_stick = { ijkl_x, ijkl_y };
        state.buttons |= stick_direction_bits(state.right_stick,
            RV_ISOURCE_RIGHT_STICK_DPAD_NORTH,
            RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH,
            RV_ISOURCE_RIGHT_STICK_DPAD_WEST,
            RV_ISOURCE_RIGHT_STICK_DPAD_EAST,
            RV_ISOURCE_RIGHT_STICK_MOVE);
    }
}

} // namespace rv_3dmppc
