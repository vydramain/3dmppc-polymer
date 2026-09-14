#include "rv_pconsole/ca/rv_pcca_sw.hpp"

#include <algorithm>
#include <vector>

#include "pdk/ca/rv_loop.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

// One 60 fps frame of console timeline at RV_PCA_SAMPLE_RATE (44100 / 60).
constexpr int64_t RV_PCCA_SELFCHECK_CHUNK_FRAMES = 735;
constexpr int64_t RV_PCCA_SELFCHECK_MAX_CHUNKS = 10;

// 10 ms mono sample, half-scale so it can never itself saturate the mix.
constexpr int64_t RV_PCCA_SELFCHECK_SAMPLE_FRAMES = 441;
constexpr int16_t RV_PCCA_SELFCHECK_SAMPLE_VALUE = 16384;

bool run_once(std::vector<int16_t> &pcm_out, int64_t &chunk_count, int64_t &finished_chunk_out)
{
    rv_pcca_conf conf;
    conf.voice_count = 4;
    conf.sound_memory_size = 65536;

    rv_pcca_sw ca(conf);
    if (!ca.valid()) {
        RV_LOG_ERR("pcca", "selfcheck: sound RAM failed to reserve");
        return false;
    }

    std::vector<int16_t> sample(static_cast<std::size_t>(RV_PCCA_SELFCHECK_SAMPLE_FRAMES),
        RV_PCCA_SELFCHECK_SAMPLE_VALUE);
    rv_sample asset{};
    asset.data = sample.data();
    asset.size = static_cast<int64_t>(sample.size()) * static_cast<int64_t>(sizeof(int16_t));

    const int64_t addr = ca.sound_asset_malloc(asset.size);
    if (addr <= 0) {
        RV_LOG_ERR("pcca", "selfcheck: sound_asset_malloc failed ({})", addr);
        return false;
    }

    if (ca.sound_asset_write(addr, &asset) != RV_OK) {
        RV_LOG_ERR("pcca", "selfcheck: sound_asset_write failed");
        return false;
    }

    // Envelope: ar=dr=sr=rr=0, sl=full scale. Attack and decay are both
    // instant (0 ms), so the envelope reaches unity on the very first output
    // frame; sr=0 holds sustain at that level forever instead of ramping it
    // down. The voice therefore sounds at full level for the whole sample and,
    // because it loops RV_LOOP_NONE, the data running out retires it directly
    // (rv_pcvoice::mix) without ever entering a release ramp — so it ends right
    // after the last sample frame regardless of rr.
    rv_voice_conf voice{};
    voice.voice = 1;
    voice.loop_type = RV_LOOP_NONE;
    voice.sample_address = addr;
    voice.ar = 0;
    voice.dr = 0;
    voice.sr = 0;
    voice.rr = 0;
    voice.sl = 32767;
    voice.volume = 32767;
    voice.volume_l = 32767;
    voice.volume_r = 32767;

    if (ca.voice_setup(&voice) != RV_OK) {
        RV_LOG_ERR("pcca", "selfcheck: voice_setup failed");
        return false;
    }

    if (ca.voice_play(1) != RV_OK) {
        RV_LOG_ERR("pcca", "selfcheck: voice_play failed");
        return false;
    }

    if (ca.voice_status(1) != 1) {
        RV_LOG_ERR("pcca", "selfcheck: voice not busy right after voice_play");
        return false;
    }

    if (ca.sound_asset_free(addr) != RV_ERR_BUSY) {
        RV_LOG_ERR("pcca", "selfcheck: sound_asset_free did not report RV_ERR_BUSY while playing");
        return false;
    }

    pcm_out.clear();
    chunk_count = 0;
    bool first_chunk_nonzero = false;
    bool saw_all_zero_after_finish = false;
    int64_t finished_chunk = -1; // index (0-based) of the chunk that first read idle

    std::vector<int16_t> chunk(static_cast<std::size_t>(RV_PCCA_SELFCHECK_CHUNK_FRAMES * 2), 0);

    for (int64_t i = 0; i < RV_PCCA_SELFCHECK_MAX_CHUNKS; ++i) {
        std::fill(chunk.begin(), chunk.end(), static_cast<int16_t>(0));
        ca.advance(chunk.data(), RV_PCCA_SELFCHECK_CHUNK_FRAMES);
        ++chunk_count;

        bool any_nonzero = false;
        for (int16_t v : chunk) {
            if (v != 0) {
                any_nonzero = true;
                break;
            }
        }

        if (i == 0) {
            first_chunk_nonzero = any_nonzero;
        }

        pcm_out.insert(pcm_out.end(), chunk.begin(), chunk.end());

        if (finished_chunk < 0) {
            if (ca.voice_status(1) == 0) {
                finished_chunk = i;
            }
        } else {
            // One chunk advanced past the finish, purely to verify silence.
            saw_all_zero_after_finish = !any_nonzero;
            break;
        }
    }

    if (finished_chunk < 0) {
        RV_LOG_ERR("pcca", "selfcheck: voice never finished within {} chunk(s)",
            RV_PCCA_SELFCHECK_MAX_CHUNKS);
        return false;
    }

    if (finished_chunk >= 2) {
        RV_LOG_ERR("pcca", "selfcheck: voice took {} chunk(s) to finish, expected at most 2",
            finished_chunk + 1);
        return false;
    }

    if (!first_chunk_nonzero) {
        RV_LOG_ERR("pcca", "selfcheck: first chunk was silent");
        return false;
    }

    if (!saw_all_zero_after_finish) {
        RV_LOG_ERR("pcca", "selfcheck: chunk after voice finished was not all zero");
        return false;
    }

    if (ca.sound_asset_free(addr) != RV_OK) {
        RV_LOG_ERR("pcca", "selfcheck: sound_asset_free failed once the voice was idle");
        return false;
    }

    finished_chunk_out = finished_chunk;
    return true;
}

} // namespace

bool rv_pcca_sw_selfcheck()
{
    std::vector<int16_t> pcm_a;
    std::vector<int16_t> pcm_b;
    int64_t chunks_a = 0;
    int64_t chunks_b = 0;
    int64_t finished_chunk_a = -1;
    int64_t finished_chunk_b = -1;

    if (!run_once(pcm_a, chunks_a, finished_chunk_a)) {
        return false;
    }
    if (!run_once(pcm_b, chunks_b, finished_chunk_b)) {
        return false;
    }

    if (chunks_a != chunks_b) {
        RV_LOG_ERR("pcca", "selfcheck: chunk counts differ between two fresh instances ({} vs {})",
            chunks_a, chunks_b);
        return false;
    }

    if (pcm_a != pcm_b) {
        RV_LOG_ERR("pcca", "selfcheck: PCM differs between two fresh instances given the same inputs");
        return false;
    }

    RV_LOG_INFO("pcca", "selfcheck ok (voice finished within chunk {} of {} frame(s))",
        finished_chunk_a + 1, RV_PCCA_SELFCHECK_CHUNK_FRAMES);
    return true;
}

} // namespace rv_3dmppc
