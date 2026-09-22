// The SDL3 window: creation, the renderer/streaming-texture pair a finished
// frame lands in, and the keyboard/mouse overlay that only exists while a
// window does.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include <cmath>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

// What the keyboard overlay on port 0 is able to report. A keyboard has no
// analog anything, but the layout below drives the sticks digitally, so the
// stick sources are advertised honestly - a disc that asks "can port 0 give me
// a left stick?" gets yes, and gets +-1 values.
constexpr uint64_t RV_PCWINDOW_SDL3_KEYBOARD_ABILITIES =
    RV_ISOURCE_FRONT_BTTN_SOUTH | RV_ISOURCE_FRONT_BTTN_EAST | RV_ISOURCE_FRONT_BTTN_WEST |
    RV_ISOURCE_FRONT_BTTN_NORTH | RV_ISOURCE_BUMPER_LEFT | RV_ISOURCE_BUMPER_RIGHT |
    RV_ISOURCE_MENU_BTTN_MENU | RV_ISOURCE_MENU_BTTN_VIEW | RV_ISOURCE_DPAD_MOVE |
    RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_EAST |
    RV_ISOURCE_LEFT_STICK_MOVE | RV_ISOURCE_LEFT_STICK_DPAD_NORTH |
    RV_ISOURCE_LEFT_STICK_DPAD_SOUTH | RV_ISOURCE_LEFT_STICK_DPAD_WEST |
    RV_ISOURCE_LEFT_STICK_DPAD_EAST | RV_ISOURCE_RIGHT_STICK_MOVE |
    RV_ISOURCE_RIGHT_STICK_DPAD_NORTH | RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH |
    RV_ISOURCE_RIGHT_STICK_DPAD_WEST | RV_ISOURCE_RIGHT_STICK_DPAD_EAST;

// sectorization. An analog stick becomes four digital directions by
// comparing |x| against |y|: the dominant axis picks the sector, its sign
// picks the side. This is the diagonal-free 4-way reading; a disc that wants
// the diagonals reads the raw axes instead.
uint64_t stick_direction_bits(rv_iaxes axes, uint64_t north, uint64_t south, uint64_t west,
    uint64_t east, uint64_t move)
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

int64_t rv_pcwindow_sdl3::open(const char *title, int64_t screen_width, int64_t screen_height,
    uint64_t scale)
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

    const int window_w = static_cast<int>(screen_width * static_cast<int64_t>(scale));
    const int window_h = static_cast<int>(screen_height * static_cast<int64_t>(scale));
    window_ = SDL_CreateWindow(title, window_w, window_h, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        RV_LOG_ERR("pcplatform", "SDL_CreateWindow failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        RV_LOG_ERR("pcplatform", "SDL_CreateRenderer failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    // The frame is always the native console resolution; the window is just a
    // magnifying glass over it. Integer scaling keeps the pixels square and
    // crisp instead of smearing them across a non-multiple window size.
    SDL_SetRenderLogicalPresentation(renderer_, static_cast<int>(screen_width),
        static_cast<int>(screen_height), SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(screen_width), static_cast<int>(screen_height));
    if (!texture_) {
        RV_LOG_ERR("pcplatform", "SDL_CreateTexture failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);

    screen_width_ = screen_width;
    screen_height_ = screen_height;

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

    SDL_UpdateTexture(texture_, nullptr, argb, static_cast<int>(screen_width_ * 4));
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

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
    const float wasd_x =
        (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    const float wasd_y =
        (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    const float ijkl_x =
        (keys[SDL_SCANCODE_L] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_J] ? 1.0f : 0.0f);
    const float ijkl_y =
        (keys[SDL_SCANCODE_I] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_K] ? 1.0f : 0.0f);

    if (wasd_x != 0.0f || wasd_y != 0.0f) {
        state.left_stick = { wasd_x, wasd_y };
        state.buttons |= stick_direction_bits(
            state.left_stick, RV_ISOURCE_LEFT_STICK_DPAD_NORTH, RV_ISOURCE_LEFT_STICK_DPAD_SOUTH,
            RV_ISOURCE_LEFT_STICK_DPAD_WEST, RV_ISOURCE_LEFT_STICK_DPAD_EAST,
            RV_ISOURCE_LEFT_STICK_MOVE);
    }
    if (ijkl_x != 0.0f || ijkl_y != 0.0f) {
        state.right_stick = { ijkl_x, ijkl_y };
        state.buttons |= stick_direction_bits(
            state.right_stick, RV_ISOURCE_RIGHT_STICK_DPAD_NORTH,
            RV_ISOURCE_RIGHT_STICK_DPAD_SOUTH, RV_ISOURCE_RIGHT_STICK_DPAD_WEST,
            RV_ISOURCE_RIGHT_STICK_DPAD_EAST, RV_ISOURCE_RIGHT_STICK_MOVE);
    }
}

} // namespace rv_3dmppc
