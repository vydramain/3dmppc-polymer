// Solidmaid: Alkoldun Vasiliusavich — the reference disc. Implements the console
// Disc ABI and orchestrates the game systems each frame. OWNED BY THE
// ORCHESTRATOR (integration point): it only wires the zones together.
#pragma once

#include "game/context.hpp"
#include "game/game_state.hpp"
#include "game/scene_render.hpp"
#include "platform/disc.hpp"

namespace rv_3dmppc {

class SolidDisc final : public Disc {
public:
    const char* title() const override { return "Solidmaid: Alkoldun Vasiliusavich"; }

    void boot(const DiscServices& services) override;
    void update(const InputState& in, float dt) override;
    void render(rv_Framebuffer& fb) override;
    bool finished() const override { return quit_; }

    // Read-only view for QA/integration harnesses (not used by the console).
    const GameState& debugState() const { return state_; }

private:
    GameState state_;
    GameContext ctx_;
    SceneRenderer renderer_;
    bool quit_ = false;
    float lastFrameMs_ = 0.0f;
    bool debugOverlay_ = false;
};

}  // namespace rv_3dmppc
