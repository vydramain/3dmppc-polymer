// The SDL3 platform: bring-up per subsystem, the event pump, and teardown
// order. Window, gamepad and audio bring-up are independent of one another -
// SDL's own subsystem init is reference counted, so a machine with no sound
// card still gets its window, a machine with no gamepad driver still gets its
// window and sound.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3.hpp"

#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

// Destructor order: audio stream, pads, then the window's texture/renderer/
// window, then SDL_Quit(). Nothing runs in this body - members are destroyed
// in reverse declaration order (audio_, gamepads_, window_, quit_guard_ last),
// each one's own destructor tearing down its SDL handles first, and only once
// all of them are gone does quit_guard_ call SDL_Quit(). See the member order
// and the comment on rv_pcsdl3_quit_guard in the detail header.
rv_pcplatform_sdl3::~rv_pcplatform_sdl3() = default;

void rv_pcplatform_sdl3::pump()
{
    // SDL_INIT_GAMEPAD implies the events subsystem, so the queue exists (and
    // is worth draining for arrival/departure and power-off events) even when
    // video never came up - input must not depend on a window.
    if (!video_up_ && !gamepad_up_) {
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            window_.note_close();
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            gamepads_.add(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            gamepads_.remove(event.gdevice.which);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            window_.add_mouse(event.motion.xrel, event.motion.yrel);
            break;
        default:
            break;
        }
    }

    // snapshot polling. The event stream above only tracks device
    // arrival/departure and mouse motion; every device's STATE is re-read
    // wholesale every frame, which is exactly what rv_cio promises - an
    // instantaneous level, never an edge.
    gamepads_.poll_all();
    if (video_up_) {
        window_.snapshot_keyboard();
    }
}

rv_pcwindow &rv_pcplatform_sdl3::window()
{
    return window_;
}

rv_pcgamepads &rv_pcplatform_sdl3::gamepads()
{
    return gamepads_;
}

rv_pcaudio_sink &rv_pcplatform_sdl3::audio()
{
    return audio_;
}

std::unique_ptr<rv_pcplatform> rv_pcplatform_sdl3_make(const rv_pcplatform_wants &wants)
{
    // Signals belong to rv_pcsignals, installed before any platform comes up;
    // this keeps SDL off SIGINT/SIGTERM entirely rather than racing to
    // restore the default disposition after the fact.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    auto platform = std::make_unique<rv_pcplatform_sdl3>();

    platform->window_.set_wanted(wants.window);

    if (wants.window) {
        platform->video_up_ = SDL_InitSubSystem(SDL_INIT_VIDEO);
        platform->quit_guard_.armed = platform->quit_guard_.armed || platform->video_up_;
        if (!platform->video_up_) {
            RV_LOG_WARN("pcplatform", "SDL_INIT_VIDEO failed: {}", SDL_GetError());
        } else {
            SDL_Rect display_bounds{};
            SDL_DisplayID display = SDL_GetPrimaryDisplay();
            if (display != 0 && SDL_GetDisplayBounds(display, &display_bounds)) {
                RV_LOG_INFO("pcplatform", "display bounds measured at {}x{}", display_bounds.w,
                    display_bounds.h);
            } else {
                RV_LOG_WARN("pcplatform", "SDL_GetDisplayBounds failed: {}", SDL_GetError());
            }
        }
        platform->window_.set_video_ready(platform->video_up_);
    }

    if (wants.gamepads) {
        platform->gamepad_up_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        platform->quit_guard_.armed = platform->quit_guard_.armed || platform->gamepad_up_;
        if (!platform->gamepad_up_) {
            RV_LOG_WARN("pcplatform", "SDL_INIT_GAMEPAD failed: {}, pads will read as empty",
                SDL_GetError());
        } else {
            int pad_count = 0;
            SDL_JoystickID *pads = SDL_GetGamepads(&pad_count);
            if (pads) {
                for (int i = 0; i < pad_count; ++i) {
                    platform->gamepads_.add(pads[i]);
                }
                SDL_free(pads);
            }
        }
    }

    if (wants.audio) {
        const bool subsystem_ok = SDL_InitSubSystem(SDL_INIT_AUDIO);
        platform->quit_guard_.armed = platform->quit_guard_.armed || subsystem_ok;
        const bool device_ok = subsystem_ok && platform->audio_.open();
        platform->audio_up_ = subsystem_ok && device_ok;
        if (!subsystem_ok) {
            RV_LOG_WARN("pcplatform", "audio subsystem did not come up: {}", SDL_GetError());
        } else if (!device_ok) {
            RV_LOG_WARN("pcplatform", "audio device failed to open: {}", SDL_GetError());
        }
    }

    RV_LOG_INFO("pcplatform", "sdl3: window={} gamepads={} audio={}",
        wants.window ? (platform->video_up_ ? "on" : "off") : "not wanted",
        wants.gamepads ? (platform->gamepad_up_ ? "on" : "off") : "not wanted",
        wants.audio ? (platform->audio_up_ ? "on" : "off") : "not wanted");

    return platform;
}

} // namespace rv_3dmppc
