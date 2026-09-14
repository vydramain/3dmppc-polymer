#include "rv_pconsole/ca/rv_pcca_sw.hpp"

#include "pdk/ca/rv_ca.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

static_assert(rv_3dmppc::RV_PCA_SAMPLE_RATE == rv_3dmppc::RV_PCCA_PCM_RATE, "sw SPU mixes at the ca contract rate");
static_assert(rv_3dmppc::RV_PCMIXER_CHANNELS == rv_3dmppc::RV_PCCA_PCM_CHANNELS,
    "sw SPU mixes the ca contract channel count");

namespace rv_3dmppc
{

namespace
{

// Every region starts on a frame boundary: sound RAM is a stream of S16LE mono
// frames and a region that began mid-frame would shift the whole sample by half
// a value.
constexpr int64_t RV_PCCA_ALIGN = RV_PCA_FRAME_BYTES;

// The pool's first bytes are never handed out, so no live region can have
// address 0 — which is what lets a zero-initialized rv_voice_conf read as
// "sample_address not set" rather than as a real region. See rv_pcpool.hpp.
constexpr int64_t RV_PCCA_RESERVED_HEAD = 16;

// A voice mask uses bits 0..62 (pdk/rv_err.h), so 63 voices is the most any
// console can express no matter what its conf asks for.
constexpr int64_t RV_PCCA_MAX_VOICES = 63;

int64_t clamp_voice_count(int64_t wanted)
{
    if (wanted < 0) {
        return 0;
    }
    return wanted > RV_PCCA_MAX_VOICES ? RV_PCCA_MAX_VOICES : wanted;
}

// Mask with one bit per existing voice. Built by shifting rather than written
// out because `count` is a conf value; the 63 case is spelled separately since
// 1 << 63 is not a thing an int64_t may do.
int64_t voices_mask(int64_t count)
{
    if (count <= 0) {
        return 0;
    }
    if (count >= RV_PCCA_MAX_VOICES) {
        return INT64_MAX;
    }
    return (static_cast<int64_t>(1) << count) - 1;
}

} // namespace

rv_pcbudget_cost rv_pcca_sw::evaluate(const rv_pdklib::rv_manifest_budget &budget)
{
    rv_pcbudget_cost cost;

    // sram_ (rv_pcpool<rv_pcca_meta>): the pool region is exactly sound_memory_size
    // bytes.
    if (rv_pcbudget_add(cost, "budget.pcca.sound_memory_size", budget.pcca.sound_memory_size)) {
        return cost;
    }

    // sram_'s block bookkeeping, reserved once at construction (rv_pcpool.hpp).
    int64_t sram_blocks_bytes = 0;
    if (rv_pcbudget_mul(cost, "budget.pcca.sound_memory_size",
            rv_pcpool<rv_pcca_meta>::max_blocks(budget.pcca.sound_memory_size, RV_PCCA_ALIGN),
            rv_pcpool<rv_pcca_meta>::block_bytes(), sram_blocks_bytes) ||
        rv_pcbudget_add(cost, "budget.pcca.sound_memory_size", sram_blocks_bytes)) {
        return cost;
    }

    // mixer_ (rv_pcmixer): one rv_pcvoice per voice, plus its fixed render
    // accumulator (RV_PCMIXER_BLOCK_FRAMES * RV_PCMIXER_CHANNELS int32_t).
    int64_t voices_bytes = 0;
    const int64_t accumulator_bytes =
        RV_PCMIXER_BLOCK_FRAMES * RV_PCMIXER_CHANNELS * static_cast<int64_t>(sizeof(int32_t));
    if (rv_pcbudget_mul(cost, "budget.pcca.voice_count", budget.pcca.voice_count,
            static_cast<int64_t>(sizeof(rv_pcvoice)), voices_bytes) ||
        rv_pcbudget_add(cost, "budget.pcca.voice_count", voices_bytes) ||
        rv_pcbudget_add(cost, "budget.pcca.voice_count", accumulator_bytes)) {
        return cost;
    }

    return cost;
}

rv_pcca_sw::rv_pcca_sw(const rv_pcca_conf &conf)
    : conf_(conf)
    , sram_(conf.sound_memory_size, RV_PCCA_ALIGN, RV_PCCA_RESERVED_HEAD)
    , mixer_(clamp_voice_count(conf.voice_count))
{
    if (!sram_.valid()) {
        RV_LOG_ERR("pcca", "failed to reserve {} byte(s) of sound RAM", conf.sound_memory_size);
    }

    if (clamp_voice_count(conf_.voice_count) != conf_.voice_count) {
        RV_LOG_WARN("pcca", "conf asks for {} voices, the mask fits {}", conf_.voice_count,
            clamp_voice_count(conf_.voice_count));
    }

    mixer_.set_muted(conf_.mute);

    // "virtual" is worth the four extra characters here: the byte count sits
    // next to a real audio device in the log, and a reader must not take it for
    // one of the host's numbers. It is this machine's self-imposed budget.
    RV_LOG_INFO("pcca", "{} voice(s), {} byte(s) of virtual sound RAM{}", mixer_.voice_count(),
        sram_.capacity(), conf_.mute ? ", muted" : "");
}

int64_t rv_pcca_sw::voice_count()
{
    return mixer_.voice_count();
}

int64_t rv_pcca_sw::sound_memory_size()
{
    return sram_.capacity();
}

int64_t rv_pcca_sw::validate_mask(int64_t voice_mask) const
{
    // Negative is not a mask at all (bits 0..62), zero names nobody, and a bit
    // above the last voice names hardware this console does not have. All three
    // are malformed calls rather than empty ones.
    if (voice_mask <= 0) {
        return RV_ERR_INVAL;
    }
    if ((voice_mask & ~voices_mask(mixer_.voice_count())) != 0) {
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

int64_t rv_pcca_sw::sound_asset_malloc(int64_t size)
{
    return sram_.malloc(size);
}

int64_t rv_pcca_sw::sound_asset_write(int64_t addr, const rv_sample *sample)
{
    if (!sample) {
        return RV_ERR_INVAL;
    }

    // THEOREM: sample pointers are stable, region contents are not. rv_pcpool
    // sizes its backing vector once, in its constructor, and never grows it —
    // malloc only splits BLOCKS, which live in a separate list. So the byte
    // pointer a playing voice holds stays valid for the whole life of the pool,
    // and the voices need no address-to-pointer resolution per frame. What is
    // NOT stable is what those bytes say: an upload into a region a voice is
    // reading this instant would be a genuine data race, so the copy happens
    // under the SPU lock. The lock is the same one the mixer takes, and nothing
    // is called through it while it is held (rv_pcmixer.hpp).
    auto guard = mixer_.acquire();

    const int64_t rc = sram_.write(addr, sample->data, sample->size);
    if (rc < 0) {
        return rc;
    }

    rv_pcca_meta *meta = sram_.region_meta(addr);
    if (!meta) {
        return RV_ERR_INVAL;
    }

    // Length is recorded only after the bytes landed, so a failed upload cannot
    // leave a region claiming a sample it does not hold. A trailing odd byte is
    // half a frame and is dropped: the console plays whole frames only.
    meta->written = true;
    meta->frames = sample->size / RV_PCA_FRAME_BYTES;

    return RV_OK;
}

int64_t rv_pcca_sw::sound_asset_free(int64_t addr)
{
    auto guard = mixer_.acquire();

    if (!sram_.region_exists(addr)) {
        return RV_ERR_INVAL;
    }

    // The contract's RV_ERR_BUSY: a region cannot be released while a voice is
    // reading it. The question is asked of the VOICES, by address — each one
    // remembers the sample_address it was armed with — and it is asked under
    // the same lock the release happens under, so a tail that decays to silence
    // between the check and the free cannot make this answer stale.
    if (mixer_.region_busy_locked(addr)) {
        return RV_ERR_BUSY;
    }

    const int64_t rc = sram_.free(addr);
    if (rc < 0) {
        return rc;
    }

    // Voices that were armed with this region but never started are disarmed
    // rather than left holding a pointer into bytes the pool may hand out
    // again: a later voice_play() then fails loudly (RV_ERR_INVAL) instead of
    // playing whatever the next upload put there.
    mixer_.disarm_region_locked(addr);

    return RV_OK;
}

int64_t rv_pcca_sw::voice_setup(const rv_voice_conf *conf)
{
    if (!conf) {
        return RV_ERR_INVAL;
    }

    const int64_t mask_rc = validate_mask(conf->voice);
    if (mask_rc < 0) {
        return mask_rc;
    }

    if (!sram_.region_exists(conf->sample_address)) {
        return RV_ERR_INVAL;
    }

    const rv_pcca_meta *meta = sram_.region_meta(conf->sample_address);
    const int64_t frames = meta && meta->written ? meta->frames : 0;
    if (frames <= 0) {
        // The address is real, so this is not RV_ERR_INVAL — the disc reserved
        // a region and armed a voice at it before uploading anything. The voice
        // arms and plays nothing.
        RV_LOG_WARN("pcca", "voice setup on empty region {}", conf->sample_address);
    }

    mixer_.setup(conf->voice, *conf, sram_.region_data(conf->sample_address), frames,
        conf->sample_address);
    return RV_OK;
}

int64_t rv_pcca_sw::voice_play(int64_t voice_mask)
{
    const int64_t mask_rc = validate_mask(voice_mask);
    if (mask_rc < 0) {
        return mask_rc;
    }

    return mixer_.play(voice_mask) ? RV_OK : RV_ERR_INVAL;
}

int64_t rv_pcca_sw::voice_stop(int64_t voice_mask)
{
    const int64_t mask_rc = validate_mask(voice_mask);
    if (mask_rc < 0) {
        return mask_rc;
    }

    return mixer_.stop(voice_mask) ? RV_OK : RV_ERR_INVAL;
}

int64_t rv_pcca_sw::voice_status(int64_t voice_mask)
{
    const int64_t mask_rc = validate_mask(voice_mask);
    if (mask_rc < 0) {
        return mask_rc;
    }

    // Always >= 0: the mask only ever carries bits 0..62, so the contract's
    // "a valid mask is never negative" holds by construction.
    return mixer_.status(voice_mask);
}

void rv_pcca_sw::advance(int16_t *out, int64_t frames)
{
    mixer_.render(out, frames);
}

} // namespace rv_3dmppc
