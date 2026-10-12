#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/rv_err.h"
#include "example-cpp-route.hpp"
#include "example-cpp-sound.hpp"

namespace example_cpp
{

namespace
{

// One step per this many metres of horizontal eye path: Solid Maid's SM_STEP_STRIDE (solid-maid/src/sm_common.hpp),
// so the footfall keeps its pace.
constexpr double RV_STEP_STRIDE = 0.95;

// Step voices: A takes even steps and B odd ones, as Solid Maid alternates its pair (sm_sound.cpp footstep).
constexpr int64_t RV_STEP_VOICE[] = { 32, 64 };
// Steps cycle through the table's voices; the variant is the step's index modulo this.
constexpr int64_t RV_STEP_VARIANTS = static_cast<int64_t>(std::size(RV_STEP_VOICE));

// Volume and envelope as Solid Maid plays a footstep: gain 1 and centre pan (sm_game.cpp, sm_sound.cpp play), so the
// level is 22000 and the envelope is flat.
constexpr int16_t RV_STEP_VOLUME = 22000;
constexpr int16_t RV_STEP_ENVELOPE = 0;
constexpr int16_t RV_STEP_MAX_LEVEL = 32767;

// The two samples of each area's floor, in rv_sound_area order: workshop concrete, street asphalt, flat linoleum.
constexpr const char *RV_STEP_ASSETS[][2] = {
    { "sfx_step_concrete_a.pcm", "sfx_step_concrete_b.pcm" },
    { "sfx_step_asphalt_a.pcm", "sfx_step_asphalt_b.pcm" },
    { "sfx_step_lino_a.pcm", "sfx_step_lino_b.pcm" },
};
static_assert(std::size(RV_STEP_ASSETS) == static_cast<std::size_t>(rv_sound_area::flat) + 1, "one pair per rv_sound_area");

} // namespace

void rv_steps_init(const std::vector<rv_route_key> &keys, rv_sound_state &state)
{
    const int64_t period = RV_ROUTE_PERIOD_FRAMES;
    state.distance.assign(static_cast<std::size_t>(period), 0.0);
    rv_cg_vec3 prev = rv_route_camera_at(keys, 0).eye;
    for (int64_t f = 1; f < period; ++f) {
        const rv_cg_vec3 eye = rv_route_camera_at(keys, f).eye;
        const double dx = static_cast<double>(eye.x) - prev.x;
        const double dz = static_cast<double>(eye.z) - prev.z;
        const std::size_t index = static_cast<std::size_t>(f);
        state.distance[index] = state.distance[index - 1] + std::sqrt(dx * dx + dz * dz);
        prev = eye;
    }
}

int64_t rv_steps_update(rv_pdko *pdk, int64_t frame, rv_sound_state &state)
{
    // Frame 0 is the seam: no step is taken across the wrap of the period.
    if (frame == 0) {
        return RV_OK;
    }
    const std::size_t index = static_cast<std::size_t>(frame);
    const double step_before = std::floor(state.distance[index - 1] / RV_STEP_STRIDE);
    const double step_now = std::floor(state.distance[index] / RV_STEP_STRIDE);
    if (step_now <= step_before) {
        return RV_OK;
    }
    const int64_t variant = static_cast<int64_t>(step_now) % RV_STEP_VARIANTS;
    const char *name = RV_STEP_ASSETS[static_cast<std::size_t>(state.areas[index])][variant];
    const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk), RV_CD_RESOURCE_AUDIO, name);
    if (addr < 0) {
        state.error_asset = name;
        return addr;
    }
    rv_voice_conf conf = {};
    conf.voice = RV_STEP_VOICE[variant];
    conf.loop_type = RV_LOOP_NONE;
    conf.sample_address = addr;
    conf.ar = RV_STEP_ENVELOPE;
    conf.dr = RV_STEP_ENVELOPE;
    conf.sr = RV_STEP_ENVELOPE;
    conf.rr = RV_STEP_ENVELOPE;
    conf.sl = RV_STEP_MAX_LEVEL;
    conf.volume = RV_STEP_VOLUME;
    conf.volume_l = RV_STEP_MAX_LEVEL;
    conf.volume_r = RV_STEP_MAX_LEVEL;
    rv_ca *ca = rv_pdko_ca(pdk);
    // As the clack: setup then play restarts a voice still playing, so no stop comes first.
    const int64_t setup_code = rv_ca_voice_setup(ca, &conf);
    if (setup_code < 0) {
        state.error_asset = name;
        return setup_code;
    }
    const int64_t play_code = rv_ca_voice_play(ca, conf.voice);
    if (play_code < 0) {
        state.error_asset = name;
        return play_code;
    }
    return RV_OK;
}

} // namespace example_cpp
