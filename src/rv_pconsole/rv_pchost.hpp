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
// The audio device is deliberately NOT part of open(): prepare() brings it up
// (stage C), whether or not a window was ever asked for. A headless run is a
// smoke test of the whole machine, and a machine whose voices never retire
// because nothing is clocking them is a different machine — the SPU's
// envelopes advance on the device thread, so the device has to exist even when
// nobody is watching the screen. rv_pcca only attaches/detaches the mixer that
// pulls from an already-running device; see the note on the failure path in
// rv_pcca.cpp for what happens when the host has no sound card at all.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdk/cio/rv_imouse.hpp"
#include "pdk/cio/rv_isource.hpp"

// No dependency on rv_pconsole_conf.hpp any more: the host is built and
// prepared BEFORE the console's configuration exists (it is the console that
// now borrows the host, not the other way around), so this layer takes its
// geometry and settings as plain arguments instead of the console's type.

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Gamepad;
struct SDL_AudioStream;

namespace rv_3dmppc
{

class rv_pcmixer;

class rv_pchost
{
public:
    rv_pchost();

    // PATTERN: RAII. The destructor is the only teardown path for the window,
    // the renderer, the streaming texture and every opened gamepad.
    ~rv_pchost();

    rv_pchost(const rv_pchost &) = delete;
    rv_pchost &operator=(const rv_pchost &) = delete;

    // Bytes of one port slot (rv_pcport, private below). Out of line in the
    // .cpp so the resource check at stage E3 (rv_pboot_check.cpp) can cost
    // configure()'s ports_.assign(iport_count, ...) without rv_pcport itself
    // becoming public.
    static int64_t port_bytes();

    // Stage C: bring up whichever SDL subsystems were asked for, measure the
    // display bounds if video came up, and — when want_audio — bring up
    // SDL_INIT_AUDIO AND open the playback device, starting it. audio_ready_
    // is true only if BOTH succeeded; whichever one failed is named in a
    // warning. Does NOT create a window, a renderer or a texture — see open()
    // for those. A subsystem that refuses to come up is recorded in its own
    // video_ready_ / gamepad_ready_ / audio_ready_ flag, not turned into a
    // failure of this call: always returns RV_OK.
    int64_t prepare(bool want_video, bool want_gamepad, bool want_audio);

    // Size the port slots, remember the screen geometry (for a headless
    // --dump-frame, which never calls open()) and remember where to dump the
    // last frame. Call once the console's configuration exists.
    void configure(int64_t screen_width, int64_t screen_height, int64_t iport_count,
        const std::string &dump_frame_path);

    // Create the window, the renderer and the streaming texture at the given
    // geometry. configure() is the normal path for
    // screen_width_/screen_height_; open() merely confirms them (and still
    // wins if a caller passes different numbers here). Video and gamepad were
    // already brought up by prepare() — this refuses immediately with
    // RV_ERR_IO if video never came up there, it does not retry bringing it
    // up itself. Not called in headless.
    int64_t open(const char *title, int64_t screen_width, int64_t screen_height, uint64_t scale);

    // True once the machine has a surface to present to.
    bool presenting() const
    {
        return renderer_ != nullptr;
    }

    // Did SDL's video subsystem come up? Distinct from presenting(), which asks
    // whether there is actually a window to draw into.
    bool video_ready() const
    {
        return video_ready_;
    }
    // Did SDL's gamepad subsystem come up? Ports read as empty when it did not.
    bool gamepad_ready() const
    {
        return gamepad_ready_;
    }
    // Did SDL's audio subsystem come up? Distinct from sounding(), which asks
    // whether a device is actually pulling frames.
    bool audio_ready() const
    {
        return audio_ready_;
    }

    // The physical display's bounds, measured once right after SDL_VIDEO comes
    // up. Zero/zero when video never came up (headless, or SDL_GetDisplayBounds
    // itself failed) — nothing downstream reads these yet.
    void display_bounds(int64_t &width, int64_t &height) const
    {
        width = display_width_;
        height = display_height_;
    }

    // Drain SDL's event queue and re-poll every device, rebuilding the port
    // snapshots. Called exactly once per frame by rv_pconsole::disc_run.
    void pump();

    // The power switch: the user closed the window. This is the CONSOLE's own
    // shutdown path and deliberately not routed through rv_de::disc_release() —
    // the contract's release query is the disc ASKING to stop, and pulling the
    // plug is not the disc's decision to make.
    bool power_off() const
    {
        return power_off_;
    }

    // Hand a finished frame to the display. `argb` is width*height pixels in
    // 0xAARRGGBB, produced by rv_pcfbuf::expand_argb().
    void present(const uint32_t *argb);

    // Write the most recently presented frame to the path given by
    // rv_pconsole_params::dump_frame_path, as a binary PPM. A developer
    // convenience: it makes "what did the console actually draw" a file that
    // can be diffed, instead of a screen capture that cannot. No-op when no
    // path was configured or nothing was ever presented.
    void dump_last_frame() const
    {
        dump_frame(last_frame_);
    }

    // --- the audio device ---
    //
    // The device itself is opened and closed by the HOST, at stage C
    // (prepare()) and in the destructor — never by rv_pcca. A disc only ever
    // attaches or detaches the mixer that the already-running device pulls
    // from.

    // Bind `mixer` as the source the callback pulls from. Safe against the
    // device thread: bound and unbound under the stream's own lock, the same
    // guard SDL uses to keep the callback out of a torn state.
    //
    // `mixer` is BORROWED and must outlive the binding — the caller (rv_pcca)
    // guarantees that by calling detach_mixer() before its mixer is destroyed.
    // No-op if the device never came up.
    void attach_mixer(rv_pcmixer &mixer);

    // Unbind the mixer. After it returns, no callback is reading it and none
    // will start with the old pointer, so the mixer may be destroyed. Safe to
    // call twice and when no mixer was ever attached.
    void detach_mixer();

    // Stop the device and forget the mixer. Safe to call twice, and safe to call
    // when audio never came up.
    void close_audio();

    // True once a playback device is actually running — from prepare() onward
    // when audio came up, whether or not any mixer is attached.
    bool sounding() const
    {
        return audio_stream_ != nullptr;
    }

    // --- what rv_pccio reads ---

    // Snapshot of `port`, already mapped into the rv_isource vocabulary. An
    // out-of-range index yields the zeroed state the contract mandates.
    const rv_pdk::rv_istate &port_state(int64_t port) const;

    // Static capability mask of `port`: which sources this concrete device can
    // report at all. Zero for an empty or out-of-range slot.
    uint64_t port_abilities(int64_t port) const;

    // Relative mouse motion accumulated since the previous call; clears the
    // accumulator (rv_cio::imouse semantics).
    rv_pdk::rv_imouse consume_mouse();

    // Drive `port`'s rumble motors for `duration_ms`. Returns RV_OK, or
    // RV_ERR_INVAL when the slot holds no gamepad, RV_ERR_IO if SDL refuses.
    int64_t rumble(int64_t port, uint16_t left, uint16_t right, uint16_t duration_ms);
    int64_t rumble_triggers(int64_t port, uint16_t left, uint16_t right, uint16_t duration_ms);

private:
    // One controller port slot. Slots are STABLE: index N is always the same
    // player, an unplugged pad leaves its slot empty rather than renumbering
    // the others (pdk/cio/rv_cio.hpp).
    struct rv_pcport {
        SDL_Gamepad *pad = nullptr;
        uint32_t joystick_id = 0;
        uint64_t abilities = 0;
        rv_pdk::rv_istate state{};
    };

    void dump_frame(const uint32_t *argb) const;

    void adopt_gamepad(uint32_t joystick_id);
    void release_gamepad(uint32_t joystick_id);
    void poll_gamepad(rv_pcport &port);
    void overlay_keyboard(rv_pcport &port);

    // Bring up an SDL subsystem, remembering that SDL now needs quitting. SDL's
    // own init is reference counted, so the video path, the gamepad path and
    // the audio path can each ask for what they need without knowing about one
    // another — and a machine with no sound card still gets its window, a
    // machine with no gamepad driver still gets its window and sound.
    bool ensure_sdl(uint32_t flags);

    // Open the playback device and start its stream. No mixer involved — the
    // callback runs safely with none attached (fill_audio() fills silence).
    // Returns false when SDL has no device to give.
    bool open_audio_device();

    // What SDL calls on ITS OWN THREAD when the device wants more data. Every
    // line it reaches is either this class's audio state (touched nowhere else)
    // or rv_pcmixer::render, which takes the SPU lock. See rv_pcmixer.hpp.
    static void audio_stream_callback(void *userdata, SDL_AudioStream *stream,
        int additional_amount, int total_amount);
    void fill_audio(SDL_AudioStream *stream, int wanted_bytes);

    int64_t screen_width_ = 0;
    int64_t screen_height_ = 0;

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;

    // Per-subsystem outcome, kept apart so a caller can tell WHAT came up
    // rather than just "some of SDL did". video_ready_ gates the event pump
    // and the presentation path; gamepad_ready_ failing is not an open()
    // failure — the port slots just stay empty, as they already do at zero
    // gamepads; audio_ready_ mirrors what open_audio() managed.
    bool video_ready_ = false;
    bool gamepad_ready_ = false;
    bool audio_ready_ = false;
    bool power_off_ = false;

    // The physical display's measured bounds; see display_bounds(). Zero until
    // VIDEO comes up and SDL_GetDisplayBounds succeeds.
    int64_t display_width_ = 0;
    int64_t display_height_ = 0;

    std::vector<rv_pcport> ports_;
    rv_pdk::rv_istate empty_state_{}; // what an out-of-range port reads as

    // Frame dumping. `last_frame_` is BORROWED from rv_pccv's framebuffer, which
    // outlives the host's use of it: the pointer is only ever read inside
    // dump_last_frame(), which the frame loop calls while the disc is still
    // alive. Nothing is copied per frame — presenting must stay cheap.
    std::string dump_path_;
    const uint32_t *last_frame_ = nullptr;

    // The device side. `audio_mixer_` is borrowed from rv_pcca; `audio_block_`
    // is the staging buffer the callback fills, allocated once at open_audio()
    // so the device thread never touches the heap.
    SDL_AudioStream *audio_stream_ = nullptr;
    rv_pcmixer *audio_mixer_ = nullptr;
    std::vector<int16_t> audio_block_;

    // SDL reports motion as float pixels; accumulating in float keeps sub-pixel
    // movement from being rounded away frame after frame.
    float mouse_dx_ = 0.0f;
    float mouse_dy_ = 0.0f;
};

} // namespace rv_3dmppc
