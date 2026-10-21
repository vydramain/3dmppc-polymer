#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_math/rv_fx.hpp"
#include "example-cpp-pdkkit-route.hpp"
#include "example-cpp-pdkkit-sound.hpp"

namespace example_cpp
{

namespace
{

// One step per 0.95 m of horizontal eye path, in Q20.12 raw: 0.95 * 4096 = 3891.2, rounded. Solid Maid's
// SM_STEP_STRIDE (solid-maid/src/sm_common.hpp), so the footfall keeps its pace.
constexpr rv_pdklib::fx RV_STEP_STRIDE = 3891;

// Step voices: A takes even steps and B odd ones, as Solid Maid alternates its pair (sm_sound.cpp footstep).
constexpr int64_t RV_STEP_VOICE[] = { 32, 64 };
// Steps cycle through the table's voices; the variant is the step's index modulo this.
constexpr int64_t RV_STEP_VARIANTS = static_cast<int64_t>(std::size(RV_STEP_VOICE));

// Volume and envelope as Solid Maid plays a footstep: gain 1 and centre pan (sm_game.cpp, sm_sound.cpp play), so the
// level is 22000 and the envelope is flat.
constexpr int16_t RV_STEP_VOLUME = 22000;
constexpr int16_t RV_STEP_ENVELOPE = 0;
// Full level of the int16 sl, volume_l and volume_r fields: the loudest value a step can carry without wrapping.
constexpr int16_t RV_STEP_MAX_LEVEL = 32767;

// The two samples of each area's floor, in rv_sound_area order: workshop concrete, street asphalt, flat linoleum.
constexpr const char *RV_STEP_ASSETS[][2] = {
    { "sfx_step_concrete_a.pcm", "sfx_step_concrete_b.pcm" },
    { "sfx_step_asphalt_a.pcm", "sfx_step_asphalt_b.pcm" },
    { "sfx_step_lino_a.pcm", "sfx_step_lino_b.pcm" },
};
static_assert(std::size(RV_STEP_ASSETS) == static_cast<std::size_t>(rv_sound_area::flat) + 1, "one pair per rv_sound_area");

} // namespace

int64_t rv_steps_init(const std::vector<rv_route_key> &keys, rv_sound_state &state)
{
    const int64_t period = RV_ROUTE_PERIOD_FRAMES;
    rv_pdklib::fx_vec3 eye = {};
    const int first_code = rv_route_eye_at(keys, 0, eye);
    if (first_code != RV_OK) {
        return fail(state, "step path", first_code);
    }
    // The path is horizontal: each eye is taken with its height dropped, so every segment is (dx, 0, dz).
    rv_pdklib::fx_vec3 prev = { eye.x, 0, eye.z };
    std::vector<rv_pdklib::fx> distance(static_cast<std::size_t>(period), 0);
    // Summed in int64, so a total beyond fx is seen before it is stored.
    int64_t sum = 0;
    for (int64_t f = 1; f < period; ++f) {
        const int eye_code = rv_route_eye_at(keys, f, eye);
        if (eye_code != RV_OK) {
            return fail(state, "step path", eye_code);
        }
        const rv_pdklib::fx_vec3 ground = { eye.x, 0, eye.z };
        rv_pdklib::fx_vec3 segment = {};
        const int sub_code = rv_pdklib::fx_vec3_sub(ground, prev, segment);
        if (sub_code != RV_OK) {
            return fail(state, "step path", sub_code);
        }
        rv_pdklib::fx length = 0;
        const int length_code = rv_pdklib::fx_vec3_length(segment, length);
        if (length_code != RV_OK) {
            return fail(state, "step path", length_code);
        }
        sum += length;
        rv_pdklib::fx total = 0;
        const int total_code = rv_pdklib::fx_from_raw(sum, total);
        if (total_code != RV_OK) {
            return fail(state, "step path", total_code);
        }
        distance[static_cast<std::size_t>(f)] = total;
        prev = ground;
    }
    state.distance = std::move(distance);
    return RV_OK;
}

int64_t rv_steps_update(rv_pdko *pdk, int64_t frame, rv_sound_state &state)
{
    // Frame 0 is the seam: no step is taken across the wrap of the period.
    if (frame == 0) {
        return RV_OK;
    }
    const std::size_t index = static_cast<std::size_t>(frame);
    // Raw division floors: the distance is never negative.
    const int64_t step_before = state.distance[index - 1] / RV_STEP_STRIDE;
    const int64_t step_now = state.distance[index] / RV_STEP_STRIDE;
    if (step_now <= step_before) {
        return RV_OK;
    }
    const int64_t variant = step_now % RV_STEP_VARIANTS;
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
