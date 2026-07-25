// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 7 — хост SDL: презентация, снимки контроллеров, звуковой поток.
// ──────────────────────────────────────────────────────────────────────────────
//
// The console's host layer: the ONLY place in the tree that knows SDL exists.
// Everything above it — the controllers, the frame loop, the disc — speaks PDK
// types and console-internal types, never SDL ones. That is why this header
// forward-declares the SDL handles instead of including <SDL3/SDL.h>: the
// dependency stops at rv_pchost.cpp.
//
// Three responsibilities, all of which are "the machine's shell" rather than a
// contract subsystem:
//   * the window / renderer / streaming texture that a finished frame lands in;
//   * the event pump, which turns SDL's event stream into the instantaneous
//     controller SNAPSHOTS rv_cio promises (see pdk/cio/rv_cio.hpp);
//   * the audio device, which pulls finished stereo frames out of the SPU's
//     mixer from a thread of SDL's own.
//
// PATTERN: null object. A headless run never calls open(), so the host stays in
// its "no window, no input, never powers off" state and every call below is a
// no-op that returns zeroes. The callers have no headless branch.
//
// The audio device is deliberately NOT part of that: open_audio() is a separate
// entry point that rv_pcca calls when it is constructed, whether or not a window
// was ever asked for. A headless run is a smoke test of the whole machine, and a
// machine whose voices never retire because nothing is clocking them is a
// different machine — the SPU's envelopes advance on the device thread, so the
// device has to exist even when nobody is watching the screen. See the note on
// the failure path in rv_pcca.cpp for what happens when the host has no sound
// card at all.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdk/cio/rv_imouse.hpp"
#include "pdk/cio/rv_isource.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Gamepad;
struct SDL_AudioStream;

namespace rv_3dmppc {

class rv_pcmixer;

class rv_pchost {
   public:
    explicit rv_pchost(const rv_pconsole_conf& conf);

    // PATTERN: RAII. The destructor is the only teardown path for the window,
    // the renderer, the streaming texture and every opened gamepad.
    ~rv_pchost();

    rv_pchost(const rv_pchost&) = delete;
    rv_pchost& operator=(const rv_pchost&) = delete;

    // Bring up SDL, the window and the presentation texture. Returns RV_OK, or
    // RV_ERR_IO if SDL refuses (no display, no driver). Not called in headless.
    int64_t open(const char* title, uint64_t scale);

    // True once the machine has a surface to present to.
    bool presenting() const { return renderer_ != nullptr; }

    // Drain SDL's event queue and re-poll every device, rebuilding the port
    // snapshots. Called exactly once per frame by rv_pconsole::disc_run.
    void pump();

    // The power switch: the user closed the window. This is the CONSOLE's own
    // shutdown path and deliberately not routed through rv_de::disc_release() —
    // the contract's release query is the disc ASKING to stop, and pulling the
    // plug is not the disc's decision to make.
    bool power_off() const { return power_off_; }

    // Hand a finished frame to the display. `argb` is width*height pixels in
    // 0xAARRGGBB, produced by rv_pcfbuf::expand_argb().
    void present(const uint32_t* argb);

    // NEUROSLOP-BEGIN (claude-opus-5)
    // Write the most recently presented frame to the path given by
    // rv_pconsole_params::dump_frame_path, as a binary PPM. A devkit
    // convenience: it makes "what did the console actually draw" a file that
    // can be diffed, instead of a screen capture that cannot. No-op when no
    // path was configured or nothing was ever presented.
    void dump_last_frame() const { dump_frame(last_frame_); }
    // NEUROSLOP-END

    // --- the audio device ---

    // Open the playback device and start pulling from `mixer`. Independent of
    // open(): the SPU needs a clock even in a headless run.
    //
    // `mixer` is BORROWED and must outlive the stream — the caller (rv_pcca)
    // guarantees that by calling close_audio() before its mixer is destroyed.
    // Returns RV_OK, or RV_ERR_IO when SDL has no device to give.
    int64_t open_audio(rv_pcmixer& mixer);

    // Stop the device and forget the mixer. Safe to call twice, and safe to call
    // when audio never came up. After it returns, no callback is in flight and
    // none will start, so the mixer may be destroyed.
    void close_audio();

    // True once frames are actually leaving the machine.
    bool sounding() const { return audio_stream_ != nullptr; }

    // --- what rv_pccio reads ---

    // Snapshot of `port`, already mapped into the rv_isource vocabulary. An
    // out-of-range index yields the zeroed state the contract mandates.
    const rv_istate& port_state(int64_t port) const;

    // Static capability mask of `port`: which sources this concrete device can
    // report at all. Zero for an empty or out-of-range slot.
    uint64_t port_abilities(int64_t port) const;

    // Relative mouse motion accumulated since the previous call; clears the
    // accumulator (rv_cio::imouse semantics).
    rv_imouse consume_mouse();

    // Drive `port`'s rumble motors for `duration_ms`. Returns RV_OK, or
    // RV_ERR_INVAL when the slot holds no gamepad, RV_ERR_IO if SDL refuses.
    int64_t rumble(int64_t port, uint16_t left, uint16_t right, uint16_t duration_ms);
    int64_t rumble_triggers(int64_t port, uint16_t left, uint16_t right, uint16_t duration_ms);

   private:
    // One controller port slot. Slots are STABLE: index N is always the same
    // player, an unplugged pad leaves its slot empty rather than renumbering
    // the others (pdk/cio/rv_cio.hpp).
    struct rv_pcport {
        SDL_Gamepad* pad = nullptr;
        uint32_t joystick_id = 0;
        uint64_t abilities = 0;
        rv_istate state{};
    };

    // NEUROSLOP-BEGIN (claude-opus-5)
    void dump_frame(const uint32_t* argb) const;
    // NEUROSLOP-END

    void adopt_gamepad(uint32_t joystick_id);
    void release_gamepad(uint32_t joystick_id);
    void poll_gamepad(rv_pcport& port);
    void overlay_keyboard(rv_pcport& port);

    // Bring up an SDL subsystem, remembering that SDL now needs quitting. SDL's
    // own init is reference counted, so the video path and the audio path can
    // each ask for what they need without knowing about the other — and a
    // machine with no sound card still gets its window.
    bool ensure_sdl(uint32_t flags);

    // What SDL calls on ITS OWN THREAD when the device wants more data. Every
    // line it reaches is either this class's audio state (touched nowhere else)
    // or rv_pcmixer::render, which takes the SPU lock. See rv_pcmixer.hpp.
    static void audio_stream_callback(void* userdata, SDL_AudioStream* stream,
                                      int additional_amount, int total_amount);
    void fill_audio(SDL_AudioStream* stream, int wanted_bytes);

    int64_t screen_width_;
    int64_t screen_height_;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool sdl_ready_ = false;
    bool power_off_ = false;

    std::vector<rv_pcport> ports_;
    rv_istate empty_state_{};  // what an out-of-range port reads as

    // NEUROSLOP-BEGIN (claude-opus-5)
    // Frame dumping. `last_frame_` is BORROWED from rv_pccv's framebuffer, which
    // outlives the host's use of it: the pointer is only ever read inside
    // dump_last_frame(), which the frame loop calls while the disc is still
    // alive. Nothing is copied per frame — presenting must stay cheap.
    std::string dump_path_;
    const uint32_t* last_frame_ = nullptr;
    // NEUROSLOP-END

    // The device side. `audio_mixer_` is borrowed from rv_pcca; `audio_block_`
    // is the staging buffer the callback fills, allocated once at open_audio()
    // so the device thread never touches the heap.
    SDL_AudioStream* audio_stream_ = nullptr;
    rv_pcmixer* audio_mixer_ = nullptr;
    std::vector<int16_t> audio_block_;

    // SDL reports motion as float pixels; accumulating in float keeps sub-pixel
    // movement from being rounded away frame after frame.
    float mouse_dx_ = 0.0f;
    float mouse_dy_ = 0.0f;
};

}  // namespace rv_3dmppc
