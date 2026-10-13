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
    int64_t open(const char *title, int64_t screen_width, int64_t screen_height, uint64_t scale) override;
    bool presenting() const override;
    void present(const uint32_t *argb) override;
    bool close_requested() const override;
    uint64_t keyboard_abilities() const override;
    rv_istate keyboard_state() const override;
    rv_imouse consume_mouse() override;
    uint32_t consume_pause_requests() override;

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
    // Counts one Pause-key DOWN edge. The pump() event loop already skips
    // SDL's key-repeat events before calling this, so every call here is a
    // fresh press, never a hold.
    void note_pause_request();
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
    uint32_t pause_requests_ = 0;
};

class rv_pcgamepads_sdl3 final : public rv_pcgamepads
{
public:
    uint64_t generation() const override;
    const std::vector<uint32_t> &connected() const override;
    uint64_t abilities(uint32_t id) const override;
    rv_istate state(uint32_t id) const override;
    int64_t rumble(uint32_t id, uint16_t left, uint16_t right, uint16_t duration_ms) override;
    int64_t rumble_triggers(uint32_t id, uint16_t left, uint16_t right, uint16_t duration_ms) override;

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

// Web has one thread: SDL calls the get callback from onaudioprocess on the
// main thread, so the web device pulls through it. Native pulls from
// service() on the console's loop instead, and its stream has no callback.
#if defined(__EMSCRIPTEN__)
constexpr bool RV_PCAUDIO_SDL3_CALLBACK_PULL = true;
#else
constexpr bool RV_PCAUDIO_SDL3_CALLBACK_PULL = false;
#endif

// Stereo frames per call into the attached source. The web callback asks for
// whatever SDL needs, so it is served in pieces of this size.
constexpr int64_t RV_PCAUDIO_SDL3_CHUNK_FRAMES = 512;

// Bytes in one interleaved stereo int16 frame. SDL counts bytes; the console
// counts frames, so every conversion goes through this.
constexpr int RV_PCAUDIO_SDL3_FRAME_BYTES = static_cast<int>(RV_PCPLATFORM_PCM_CHANNELS * sizeof(int16_t));

// Native queue target, in stereo frames: 2 frames of 735 at 60 Hz, the
// console's old queue target. A constant on purpose, not derived from the
// device block size.
constexpr int64_t RV_PCAUDIO_SDL3_NATIVE_TARGET_FRAMES = 1470;

class rv_pcaudio_sdl3 final : public rv_pcaudio_sink
{
public:
    bool available() const override;
    void attach_source(rv_pcaudio_fill_fn fill, void *context) override;
    void service() override;
    int pause() override;
    int resume() override;

    ~rv_pcaudio_sdl3() override;

    // Opens SDL_INIT_AUDIO's default playback device stream. Returns RV_OK if
    // streaming, RV_ERR_IO if device failed; the caller (rv_pcplatform_sdl3::make)
    // logs the failure with SDL_GetError().
    int open();

private:
    // Stereo frames waiting in the device stream's queue; 0 without a stream.
    int64_t queued() const;
    // Queues interleaved stereo frames into the device stream; one warning on the first failure.
    void put(const int16_t *interleaved, int64_t frames);

    // SDL's get callback for the web device; `userdata` is this sink.
    static void SDLCALL pull_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount);

    // Fills `frames` from the source in chunks and queues them with put().
    // Nothing without an attached source.
    void pull_frames(int64_t frames);

    SDL_AudioStream *stream_ = nullptr;
    bool warned_once_ = false;
    rv_pcaudio_fill_fn fill_ = nullptr;
    void *fill_context_ = nullptr;
    int16_t fill_buffer_[RV_PCAUDIO_SDL3_CHUNK_FRAMES * RV_PCPLATFORM_PCM_CHANNELS]{};
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
