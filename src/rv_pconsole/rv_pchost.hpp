// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — хост SDL, презентация кадра и снимки контроллеров.
// ──────────────────────────────────────────────────────────────────────────────
//
// The console's host layer: the ONLY place in the tree that knows SDL exists.
// Everything above it — the controllers, the frame loop, the disc — speaks PDK
// types and console-internal types, never SDL ones. That is why this header
// forward-declares the SDL handles instead of including <SDL3/SDL.h>: the
// dependency stops at rv_pchost.cpp.
//
// Two responsibilities, both of which are "the machine's shell" rather than a
// contract subsystem:
//   * the window / renderer / streaming texture that a finished frame lands in;
//   * the event pump, which turns SDL's event stream into the instantaneous
//     controller SNAPSHOTS rv_cio promises (see pdk/cio/rv_cio.hpp).
//
// PATTERN: null object. A headless run never calls open(), so the host stays in
// its "no window, no input, never powers off" state and every call below is a
// no-op that returns zeroes. The callers have no headless branch.
#pragma once

#include <cstdint>
#include <vector>

#include "pdk/cio/rv_imouse.hpp"
#include "pdk/cio/rv_isource.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Gamepad;

namespace rv_3dmppc {

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

    void adopt_gamepad(uint32_t joystick_id);
    void release_gamepad(uint32_t joystick_id);
    void poll_gamepad(rv_pcport& port);
    void overlay_keyboard(rv_pcport& port);

    int64_t screen_width_;
    int64_t screen_height_;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool sdl_ready_ = false;
    bool power_off_ = false;

    std::vector<rv_pcport> ports_;
    rv_istate empty_state_{};  // what an out-of-range port reads as

    // SDL reports motion as float pixels; accumulating in float keeps sub-pixel
    // movement from being rounded away frame after frame.
    float mouse_dx_ = 0.0f;
    float mouse_dy_ = 0.0f;
};

}  // namespace rv_3dmppc
