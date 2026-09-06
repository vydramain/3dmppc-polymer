// Hardware geometry of the concrete console. Defaults = the reference machine
// from docs/platform/specs.md. Session-stable: consumed at construction, never
// swapped while a disc runs. Console-internal — never crosses into pdk/.
#pragma once

#include <cstdint>
#include <string>

namespace rv_3dmppc
{

struct rv_pcca_conf {
    int64_t voice_count = 24;
    int64_t sound_memory_size = 512 * 1024; // docs/platform/specs.md

    // Silence the output stage without changing anything a disc can observe:
    // voices still play, voice_status() still reports them busy, only the
    // device never hears them. A muted console must not become a different
    // machine from the disc's point of view.
    bool mute = false;

    // The device is never opened at all: rv_pcca::sounding_ stays false from
    // construction, exactly the degraded state "no sound card" already leaves
    // the machine in. Unlike mute this is visible to the disc (voice_status()
    // never reports busy) — it is a different machine, not a quieter one.
    bool no_audio = false;
};

struct rv_pccv_conf {
    int64_t screen_width = 320;
    int64_t screen_height = 240;
    int64_t texture_max_width = 256;
    int64_t texture_max_height = 256;
    int64_t video_memory_size = 1024 * 1024;
    int64_t frame_capacity = 4096;
    int64_t ot_bucket_count = 1024; // hidden from the contract by design

    // The depth window the ordering table spans, also hidden from the contract:
    // a disc hands rv_primitive::depth as a VALUE and never learns how it is
    // quantized. Values outside clamp to the nearest bucket (rv_primitives.hpp).
    int32_t depth_min = -32768;
    int32_t depth_max = 32767;
};

struct rv_pccio_conf {
    int64_t iport_count = 2;
};

struct rv_pccm_conf {
    int64_t card_slots = 16;
    int64_t card_slot_size = 8 * 1024;

    // Backing image for the card. Empty = "memcard.mppccard" in the working
    // directory. The card is ALWAYS inserted (rv_cm.hpp): where its bytes live
    // is the console's business and never an operation the disc invokes.
    std::string image_path;
};

// The drive exposes no geometry through its contract — this is not hardware
// shape but WHICH MEDIUM IS INSERTED, which is the console's business in
// exactly the same way the memory-card image is.
struct rv_pccd_conf {
    // Path of the mounted medium: a directory today, a .mppcdisc archive once
    // packaging lands. Empty = no disc in the drive, and every lookup then
    // legally answers RV_ERR_NOENT rather than failing.
    std::string medium_path;
};

struct rv_pconsole_params {
    bool headless = false;
    bool fixed_step = false;
    uint64_t scale = 3;
    uint64_t max_frames = 0;

    // Frame pacing. The presented console runs at target_fps; a headless run
    // ignores this and goes as fast as it can (it is a smoke test, not a game).
    // fixed_step feeds the disc exactly 1/target_fps regardless of wall clock.
    uint64_t target_fps = 60;

    // Where to write the last presented frame as a binary PPM when the run
    // ends. Empty = never. Devkit only — it is how "what did the console draw"
    // becomes a file that can be diffed instead of a screenshot that cannot.
    std::string dump_frame_path;
};

struct rv_pconsole_conf {
    rv_pcca_conf ca;
    rv_pccd_conf cd;
    rv_pccv_conf cv;
    rv_pccio_conf cio;
    rv_pccm_conf cm;

    rv_pconsole_params params;
};

} // namespace rv_3dmppc
