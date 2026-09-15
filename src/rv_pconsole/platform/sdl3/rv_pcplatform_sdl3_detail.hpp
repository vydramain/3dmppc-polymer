// The SDL3 platform's concrete classes. Included only by the .cpp files under
// this directory - this is where <SDL3/SDL.h> is allowed to appear in a
// header, because nothing outside platform/sdl3/ ever includes this file.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "pdk/cio/rv_imouse.h"
#include "pdk/cio/rv_isource.h"
#include "rv_pconsole/platform/rv_pcplatform.hpp"

namespace rv_3dmppc
{

class rv_pcwindow_sdl3 final : public rv_pcwindow
{
public:
    int64_t open(const char *title, int64_t screen_width, int64_t screen_height,
        uint64_t scale) override;
    bool presenting() const override;
    void present(const uint32_t *argb) override;
    bool close_requested() const override;
    uint64_t keyboard_abilities() const override;
    rv_istate keyboard_state() const override;
    rv_imouse consume_mouse() override;

    // Whether SDL_INIT_VIDEO came up; open() refuses when it did not. Set by
    // rv_pcplatform_sdl3::make() before open() is ever called.
    void set_video_ready(bool ready)
    {
        video_ready_ = ready;
    }

    // Whether a window was asked for at all (rv_pcplatform_wants::window).
    // Set by rv_pcplatform_sdl3::make(). A window that was never wanted makes
    // open() a successful no-op instead of the video-not-ready error path.
    void set_wanted(bool wanted)
    {
        wanted_ = wanted;
    }

    ~rv_pcwindow_sdl3() override;

    // --- what pump() drives ---
    void note_close();
    void add_mouse(float dx, float dy);
    // Re-derive keyboard_state_ from SDL_GetKeyboardState(). No-op without a
    // window: keys only ever reach a window.
    void snapshot_keyboard();

private:
    bool wanted_ = false;
    bool video_ready_ = false;
    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    int64_t screen_width_ = 0;
    int64_t screen_height_ = 0;
    bool close_requested_ = false;
    rv_istate keyboard_state_{};
    float mouse_dx_ = 0.0f;
    float mouse_dy_ = 0.0f;
};

class rv_pcgamepads_sdl3 final : public rv_pcgamepads
{
public:
    uint64_t generation() const override;
    const std::vector<uint32_t> &connected() const override;
    uint64_t abilities(uint32_t id) const override;
    rv_istate state(uint32_t id) const override;
    int64_t rumble(uint32_t id, uint16_t left, uint16_t right, uint16_t duration_ms) override;
    int64_t rumble_triggers(uint32_t id, uint16_t left, uint16_t right,
        uint16_t duration_ms) override;

    ~rv_pcgamepads_sdl3() override;

    // --- what pump() drives ---
    void add(uint32_t joystick_id);
    void remove(uint32_t joystick_id);
    void poll_all();

private:
    struct rv_pcpad_sdl3 {
        SDL_Gamepad *pad = nullptr;
        uint64_t abilities = 0;
        rv_istate state{};
    };

    std::unordered_map<uint32_t, rv_pcpad_sdl3> pads_;
    std::vector<uint32_t> connected_;
    uint64_t generation_ = 0;
    rv_istate empty_state_{};
};

class rv_pcaudio_sdl3 final : public rv_pcaudio_sink
{
public:
    bool available() const override;
    int64_t queued_frames() const override;
    void write(const int16_t *interleaved, int64_t frames) override;

    ~rv_pcaudio_sdl3() override;

    // Opens SDL_INIT_AUDIO's default playback device stream. Returns whether a
    // device is now streaming; the caller (rv_pcplatform_sdl3::make) logs the
    // failure with SDL_GetError().
    bool open();

private:
    SDL_AudioStream *stream_ = nullptr;
    bool warned_once_ = false;
};

// RAII guard for SDL_Quit(). Declared as rv_pcplatform_sdl3's FIRST member so
// it is destroyed LAST: every other member's destructor (audio stream, pads,
// window/renderer/texture) must run against a still-initialized SDL, and C++
// destroys members in reverse declaration order.
struct rv_pcsdl3_quit_guard {
    bool armed = false;

    ~rv_pcsdl3_quit_guard()
    {
        if (armed) {
            SDL_Quit();
        }
    }
};

class rv_pcplatform_sdl3 final : public rv_pcplatform
{
public:
    ~rv_pcplatform_sdl3() override;

    void pump() override;
    rv_pcwindow &window() override;
    rv_pcgamepads &gamepads() override;
    rv_pcaudio_sink &audio() override;

    // Declared first: destroyed last, after window_/gamepads_/audio_ below
    // have already torn down every SDL handle they own.
    rv_pcsdl3_quit_guard quit_guard_;

    rv_pcwindow_sdl3 window_;
    rv_pcgamepads_sdl3 gamepads_;
    rv_pcaudio_sdl3 audio_;

    // Whether pump() has anything to drain events for: gamepads must keep
    // updating without a window (SDL_INIT_GAMEPAD implies the events
    // subsystem on its own).
    bool video_up_ = false;
    bool gamepad_up_ = false;
    bool audio_up_ = false;
};

} // namespace rv_3dmppc
