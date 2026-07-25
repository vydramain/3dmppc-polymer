// Hardware geometry of the concrete console. Defaults = the reference machine
// from docs/platform/specs.md. Session-stable: consumed at construction, never
// swapped while a disc runs. Console-internal — never crosses into pdk/.
#pragma once

#include <cstdint>

namespace rv_3dmppc {

struct rv_pcca_conf {
    int64_t voice_count = 24;
    int64_t sound_memory_size = 512 * 1024;
};

struct rv_pccv_conf {
    int64_t screen_width = 320;
    int64_t screen_height = 240;
    int64_t texture_max_width = 256;
    int64_t texture_max_height = 256;
    int64_t video_memory_size = 1024 * 1024;
    int64_t frame_capacity = 4096;
    int64_t ot_bucket_count = 1024;  // hidden from the contract by design

    // NEUROSLOP-BEGIN (claude-opus-5)
    // The depth window the ordering table spans, also hidden from the contract:
    // a disc hands rv_primitive::depth as a VALUE and never learns how it is
    // quantized. Values outside clamp to the nearest bucket (rv_primitives.hpp).
    int32_t depth_min = -32768;
    int32_t depth_max = 32767;
    // NEUROSLOP-END
};

struct rv_pccio_conf {
    int64_t iport_count = 2;
};

struct rv_pccm_conf {
    int64_t card_slots = 16;
    int64_t card_slot_size = 8 * 1024;
};

struct rv_pconsole_params {
    bool headless = false;
    bool fixed_step = false;
    uint64_t scale = 3;
    uint64_t max_frames = 0;

    // NEUROSLOP-BEGIN (claude-opus-5)
    // Frame pacing. The presented console runs at target_fps; a headless run
    // ignores this and goes as fast as it can (it is a smoke test, not a game).
    // fixed_step feeds the disc exactly 1/target_fps regardless of wall clock.
    uint64_t target_fps = 60;
    // NEUROSLOP-END
};

// rv_cd has no entry: its contract exposes no geometry.
struct rv_pconsole_conf {
    rv_pcca_conf ca;
    rv_pccv_conf cv;
    rv_pccio_conf cio;
    rv_pccm_conf cm;

    rv_pconsole_params params;
};

}  // namespace rv_3dmppc
