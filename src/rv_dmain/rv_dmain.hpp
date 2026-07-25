// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 4-8 — скелетный диск как сервисный тест всех подсистем.
// ──────────────────────────────────────────────────────────────────────────────
//
// The skeleton disc, grown into the console's SERVICE TEST. It is still not a
// game: it is the smallest thing that touches every subsystem the machine
// claims to have, so a broken one shows up on screen or in the speakers rather
// than staying buried in a log.
//
//   rv_cv  — a spinning textured cube, plus three sprites showing the wrap
//            modes and the cut-out transparency rule
//   rv_cd  — reads an asset off the mounted medium and proves it arrived
//   rv_cm  — a boot counter that survives a restart
//   rv_ca  — a synthesized beep on the south button
//   rv_cio — everything above is driven from port 0
//
// BOUNDARY: this translation unit includes pdk/ (the contract) and sdk/ (the
// disc-side conveniences) and NOTHING else. No console header, no console
// logger, no SDL — the disc plays ON the console, it does not link INTO it
// (pdk/README.md). The build enforces it: the disc target links 3dmppc_pdk and
// 3dmppc_sdk and never sees the console's include path.
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
    // What the disc persists. Tiny on purpose — the point is that it survives a
    // restart, not what is in it. The magic guards against reading a slot some
    // other game wrote.
    struct rv_dmain_save {
        uint32_t magic;
        uint32_t boot_count;
    };

    void build_texture();
    void probe_drive();
    void load_save();
    void build_beep();
    void draw_cube();
    void draw_wrap_row();
    void draw_status_bars();

    // Borrowed facade — the console owns it and it stays valid until the disc
    // is torn down. Never deleted here.
    rv_pdko* pdk_ = nullptr;

    // Hardware geometry, queried once and validated against this disc's baked
    // assumptions. The contract calls these session-stable, so re-querying them
    // every frame would be pointless.
    int64_t screen_width_ = 0;
    int64_t screen_height_ = 0;
    int64_t frame_capacity_ = 0;
    int64_t iport_count_ = 0;

    // --- video ---
    std::vector<uint16_t> texels_;  // the procedural DIRECT15 test texture
    int64_t addr_texture_ = 0;      // 0 reads as "not uploaded" by design
    float spin_ = 0.0f;
    float hue_ = 0.0f;

    // --- drive ---
    int64_t asset_bytes_ = 0;           // what we read off the medium, 0 = nothing
    bool asset_ok_ = false;             // the read round-tripped and looked like itself
    bool drive_rejects_paths_ = false;  // the traversal probe answered correctly

    // --- memory card ---
    uint32_t boot_count_ = 0;
    bool card_ok_ = false;

    // --- audio ---
    int64_t addr_beep_ = 0;
    bool beep_ok_ = false;

    // Previous frame's live button mask, one entry per port. The contract hands
    // out LEVELS only, so a disc that wants edges keeps the previous snapshot
    // itself — see the THEOREM in frame_update.
    std::vector<uint64_t> prev_buttons_;

    bool release_ = false;
};

}  // namespace rv_3dmppc
