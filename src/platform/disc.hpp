// The disc ABI — the boundary between the game-agnostic console and a game.
//
// The console never knows *which* game it runs: it boots a Disc, feeds it input
// and a time step, and presents whatever it renders into the framebuffer. A disc
// receives the console's capabilities (audio, save) once, at boot. This is the
// contract docs/platform/README.md calls "disc-abi".
//
// SHARED CONTRACT — owned by the orchestrator. Console (Agent A) drives it; the
// Solidmaid disc (game/solid) implements it.
//
// DEPRECATED, dies with the MVP migration: replaced by pdk/rv_de.hpp (rv_de +
// rv_pdko instead of rv_Disc + rv_DiscServices). The PDK carries no size
// constants at all — a disc queries rv_cv::screen_width()/screen_height(); the
// literals below serve only this legacy path and die with it.
#pragma once

#include "core/audio.hpp"
#include "core/input.hpp"
#include "core/savecard.hpp"

namespace rv_3dmppc {

class rv_Framebuffer;

// Legacy: native framebuffer size of the old in-binary path.
constexpr int kNativeWidth = 320;
constexpr int kNativeHeight = 240;

// The capabilities the console hands a disc at boot.
struct rv_DiscServices {
    Audio* audio = nullptr;
    SaveCard* save = nullptr;
};

class rv_Disc {
   public:
    virtual ~rv_Disc() = default;

    // Shown in the window title / logs.
    virtual const char* title() const = 0;

    // Called once, before the first frame, with the console's services.
    virtual void boot(const rv_DiscServices& services) = 0;

    // Advance the simulation by `dt` seconds using this frame's input.
    virtual void update(const InputState& input, float dt) = 0;

    // Render the current frame into the console framebuffer.
    virtual void render(rv_Framebuffer& fb) = 0;

    // Disc may ask the console to power off (e.g. quit from a menu).
    virtual bool finished() const { return false; }
};

}  // namespace rv_3dmppc
