// 2D HUD + overlays drawn straight into the framebuffer after the 3D pass:
// crosshair, HP bar, weapon/cooldown readout, interact prompt, ritual progress,
// low-HP vignette (palette/dither), day-summary + game-over screens, and a debug
// overlay. OWNED BY AGENT E.
#pragma once

#include "core/framebuffer.hpp"

namespace rv_3dmppc {

struct GameState;

// Gameplay HUD for the current frame (reads GameState, never mutates it).
void drawHud(const GameState& gs, rv_Framebuffer& fb);

// Optional developer overlay (frame time, active enemies, player HP).
void drawDebugOverlay(const GameState& gs, rv_Framebuffer& fb, float frameMs);

}  // namespace rv_3dmppc
