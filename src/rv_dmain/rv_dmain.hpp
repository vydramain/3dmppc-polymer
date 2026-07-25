// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — диск «световое шоу»: проверка ввода, глубины и заливок.
// ──────────────────────────────────────────────────────────────────────────────
//
// A light-show disc: it draws one primitive of every kind, each at a different
// depth, over a slowly cycling background, and lets port 0's left stick drive
// the sprite. That makes it a smoke test with eyes — if the ordering table, the
// gouraud interpolation, the wireframe path, the dead zone or the button
// snapshots are broken, it is visible on screen rather than buried in a log.
//
// BOUNDARY: this translation unit includes pdk/ and the C++ standard library and
// NOTHING else. No console header, no console logger, no SDL — the disc plays ON
// the console, it does not link INTO it (pdk/README.md). The build enforces it:
// the disc target compiles with -Ipdk/include alone.
#pragma once

#include <cstdint>
#include <vector>

#include "pdk/de/rv_de.hpp"

namespace rv_3dmppc {

class rv_dmain : public rv_de {
   public:
    rv_dmain() = default;
    ~rv_dmain() override = default;

    int64_t disc_initialize(rv_pdko& pdk) override;
    void frame_update(float dt) override;
    void frame_render() override;
    bool disc_release() const override { return release_; }
    const char* disc_title() const override;

   private:
    // Borrowed facade — the console owns it and it stays valid until the disc is
    // torn down. Never deleted here.
    rv_pdko* pdk_ = nullptr;

    // Hardware geometry, queried once in disc_initialize and validated against
    // this disc's baked assumptions. Re-querying per frame would be pointless:
    // the contract calls these session-stable.
    int64_t screen_width_ = 0;
    int64_t screen_height_ = 0;
    int64_t frame_capacity_ = 0;
    int64_t iport_count_ = 0;

    // Colour phase in [0, 1) — one full trip around the hue circle.
    float hue_ = 0.0f;

    // Sprite centre and its self-drift velocity, in pixels. Kept in float so a
    // slow drift is not rounded away frame after frame; only the final position
    // handed to the GPU is integral.
    float sprite_x_ = 0.0f;
    float sprite_y_ = 0.0f;
    float drift_x_ = 47.0f;
    float drift_y_ = 31.0f;

    // Previous frame's live button mask, one entry per port. The contract hands
    // out LEVELS only, so a disc that wants edges keeps the previous snapshot
    // itself — see the THEOREM in frame_update.
    std::vector<uint64_t> prev_buttons_;

    // Set once the menu button's press EDGE is seen; polled by the console.
    bool release_ = false;
};

}  // namespace rv_3dmppc
