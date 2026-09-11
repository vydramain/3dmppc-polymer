// rv_pchost_sdl3: SDL bring-up, the window and the host's lifecycle.
#include "rv_pconsole/rv_pchost_sdl3.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/ca/rv_pcmixer.hpp"

namespace rv_3dmppc
{

// Out of line because rv_pcport is private — this is the only way stage E3's
// resource check can cost configure()'s ports_.assign(iport_count, ...).
int64_t rv_pchost_sdl3::port_bytes()
{
    return static_cast<int64_t>(sizeof(rv_pcport));
}

rv_pchost_sdl3::rv_pchost_sdl3()
{
}

rv_pchost_sdl3::~rv_pchost_sdl3()
{
    // The device thread goes first: everything below it is state a callback in
    // flight would be reading.
    close_audio();

    for (rv_pcport &port : ports_) {
        if (port.pad) {
            SDL_CloseGamepad(port.pad);
        }
    }
    if (window_) {
        SDL_DestroyWindow(window_);
    }
    if (video_ready_ || gamepad_ready_ || audio_ready_) {
        SDL_Quit();
    }
}

bool rv_pchost_sdl3::ensure_sdl(uint32_t flags)
{
    return SDL_InitSubSystem(flags);
}

void rv_pchost_sdl3::configure(int64_t iport_count)
{
    ports_.assign(static_cast<std::size_t>(iport_count > 0 ? iport_count : 0), rv_pcport{});

    // Adopt whatever is already plugged in; later arrivals come as events.
    // Skipped outright when the subsystem never came up — SDL_GetGamepads
    // would just report nothing, so this is only saving the call. Done here
    // rather than in open() because input must not depend on a window ever
    // opening.
    if (gamepad_ready_) {
        int pad_count = 0;
        SDL_JoystickID *pads = SDL_GetGamepads(&pad_count);
        if (pads) {
            for (int i = 0; i < pad_count; ++i) {
                adopt_gamepad(pads[i]);
            }
            SDL_free(pads);
        }
    }
}

int64_t rv_pchost_sdl3::prepare(bool want_video, bool want_gamepad, bool want_audio)
{
    if (want_video) {
        video_ready_ = ensure_sdl(SDL_INIT_VIDEO);
        if (!video_ready_) {
            RV_LOG_WARN("pchost", "SDL_INIT_VIDEO failed: {}", SDL_GetError());
        } else {
            SDL_Rect display_bounds{};
            SDL_DisplayID display = SDL_GetPrimaryDisplay();
            if (display != 0 && SDL_GetDisplayBounds(display, &display_bounds)) {
                display_width_ = display_bounds.w;
                display_height_ = display_bounds.h;
                RV_LOG_INFO("pchost", "display bounds measured at {}x{}", display_width_,
                    display_height_);
            } else {
                RV_LOG_WARN("pchost", "SDL_GetDisplayBounds failed: {}", SDL_GetError());
            }
        }
    }

    if (want_gamepad) {
        gamepad_ready_ = ensure_sdl(SDL_INIT_GAMEPAD);
        if (!gamepad_ready_) {
            RV_LOG_WARN("pchost", "SDL_INIT_GAMEPAD failed: {}, ports will read as empty",
                SDL_GetError());
        }
    }

    if (want_audio) {
        const bool subsystem_ok = ensure_sdl(SDL_INIT_AUDIO);
        const bool device_ok = subsystem_ok && open_audio_device();
        audio_ready_ = subsystem_ok && device_ok;
        if (!subsystem_ok) {
            RV_LOG_WARN("pchost", "audio subsystem did not come up: {}", SDL_GetError());
        } else if (!device_ok) {
            RV_LOG_WARN("pchost", "audio device failed to open: {}", SDL_GetError());
        }
    }

    return RV_OK;
}

int64_t rv_pchost_sdl3::open(const char *title, int64_t screen_width, int64_t screen_height,
    uint64_t scale)
{
    // Video already came up (or didn't) in prepare(), stage C. open() only
    // builds the window on top of that — it never retries bringing video up
    // itself. Gamepad adoption already happened in configure(), independent
    // of a window.
    if (!video_ready_) {
        RV_LOG_ERR("pchost", "video never came up at stage C, refusing to open a window");
        return RV_ERR_IO;
    }

    if (scale == 0) {
        scale = 1;
    }

    const int window_w = static_cast<int>(screen_width * static_cast<int64_t>(scale));
    const int window_h = static_cast<int>(screen_height * static_cast<int64_t>(scale));
    window_ = SDL_CreateWindow(title, window_w, window_h, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        RV_LOG_ERR("pchost", "SDL_CreateWindow failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    RV_LOG_INFO("pchost", "window {}x{} at scale {}, {} port slot(s), video={} gamepad={}",
        screen_width, screen_height, scale, ports_.size(), video_ready_, gamepad_ready_);
    return RV_OK;
}

} // namespace rv_3dmppc
