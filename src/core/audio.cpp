// SDL3-backed audio mixer — the console's SPU stand-in.
//
// Everything is synthesized procedurally (no sample files, no network): the SFX
// set is short enveloped tone/noise voices, and the three location moods (plus
// boss/victory/defeat stingers) are low-volume looping pads swapped by setMood
// with a short crossfade. A fixed voice pool mirrors the spec's 24-voice SPU.
//
// Rendering happens on SDL's audio thread via SDL_OpenAudioDeviceStream's pull
// callback; playSfx()/setMood() run on the game thread, so the shared mixer
// state is guarded by a mutex. If no device opens the whole thing degrades to a
// silent NullAudio (see makeSdlAudio), so callers never have to special-case it.
#include "core/audio.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>

#include "core/audio_backend.hpp"

namespace rv_3dmppc {

namespace {

constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;         // stereo, as per specs.md
constexpr int kVoices = 24;          // SPU voice count from specs.md
constexpr float kPi = 3.14159265358979323846f;
constexpr float kMoodGain = 0.14f;   // pads sit quietly under the SFX
constexpr float kMoodFade = 0.45f;   // crossfade time in seconds (~art-and-audio)

// Deterministic white-noise source (fixed seed — no wall-clock seeding anywhere
// in the console). Returns a sample in [-1, 1).
struct NoiseRng {
    std::uint32_t s = 0x1234567u;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(static_cast<std::int32_t>(s)) / 2147483648.0f;
    }
};

enum class Wave { Sine, Square, Triangle, Noise };

// One playing SFX voice: a single oscillator with a linear frequency sweep and
// an attack/decay envelope. Voices free themselves once their duration elapses.
struct Voice {
    bool active = false;
    Wave wave = Wave::Sine;
    float phase = 0.0f;    // oscillator phase in radians
    float freq = 440.0f;   // start frequency (Hz)
    float freqEnd = 440.0f;// end frequency (Hz) — linear sweep target
    float t = 0.0f;        // elapsed time (s)
    float dur = 0.2f;      // total duration (s)
    float amp = 0.3f;      // peak amplitude
    float attack = 0.004f; // attack time (s)
    float pan = 0.0f;      // -1 left .. +1 right
    NoiseRng noise;
};

// Per-SFX synthesis recipe.
struct SfxParams {
    Wave wave;
    float f0, f1;   // start/end frequency (Hz); ignored for noise
    float dur;      // seconds
    float amp;      // peak amplitude
    float attack;   // seconds
};

SfxParams paramsFor(Audio::Sfx s) {
    using Sfx = Audio::Sfx;
    switch (s) {
        // Dull short noise tap.
        case Sfx::Footstep:      return {Wave::Noise,    0,    0,    0.07f, 0.22f, 0.002f};
        // Rising whoosh as the brick leaves the hand.
        case Sfx::BrickThrow:    return {Wave::Triangle, 220,  480,  0.12f, 0.28f, 0.004f};
        // Hard noisy thud on contact.
        case Sfx::BrickImpact:   return {Wave::Noise,    0,    0,    0.10f, 0.42f, 0.001f};
        // Airy swing of the pipe.
        case Sfx::PipeSwing:     return {Wave::Triangle, 520,  900,  0.10f, 0.24f, 0.004f};
        // Metallic clang (square, high, quick down-sweep).
        case Sfx::PipeImpact:    return {Wave::Square,   900,  700,  0.15f, 0.34f, 0.001f};
        // Enemy grunt — pitched square dropping down.
        case Sfx::EnemyHurt:     return {Wave::Square,   300,  150,  0.15f, 0.30f, 0.004f};
        // Steady warning beep before an AoE lands.
        case Sfx::AoeWarn:       return {Wave::Sine,     880,  880,  0.30f, 0.22f, 0.010f};
        // Bright single tick per ritual step.
        case Sfx::RitualStep:    return {Wave::Sine,     523,  523,  0.15f, 0.26f, 0.006f};
        // Rising chime on ritual completion.
        case Sfx::RitualComplete:return {Wave::Sine,     523,  1046, 0.40f, 0.30f, 0.006f};
        // Low, ugly hurt tone for the player.
        case Sfx::PlayerHurt:    return {Wave::Square,   200,  90,   0.25f, 0.34f, 0.004f};
        // Crisp UI tick.
        case Sfx::UiClick:       return {Wave::Square,   1200, 1200, 0.03f, 0.20f, 0.001f};
        default:                 return {Wave::Sine,     440,  440,  0.10f, 0.20f, 0.004f};
    }
}

// A quiet, slowly-moving pad per mood: a two-partial chord with a gentle
// tremolo so the loop breathes. Returns a mono sample in roughly [-1, 1].
float moodTone(Audio::Mood m, double time) {
    using Mood = Audio::Mood;
    float base;      // root frequency (Hz)
    float fifth;     // second partial
    float tremHz;    // tremolo rate
    switch (m) {
        case Mood::Home:    base = 110.0f; fifth = 164.8f; tremHz = 0.20f; break;  // A2 + E3, calm
        case Mood::Street:  base = 146.8f; fifth = 220.0f; tremHz = 1.10f; break;  // D3 + A3, restless
        case Mood::Factory: base = 82.4f;  fifth = 110.0f; tremHz = 0.35f; break;  // E2 + A2, heavy
        case Mood::Boss:    base = 65.4f;  fifth = 98.0f;  tremHz = 0.60f; break;  // C2 + G2, ominous
        case Mood::Victory: base = 220.0f; fifth = 277.2f; tremHz = 0.25f; break;  // A3 + C#4, bright major
        case Mood::Defeat:  base = 98.0f;  fifth = 116.5f; tremHz = 0.15f; break;  // G2 + A#2, minor
        case Mood::Silent:
        default:            return 0.0f;
    }
    const float w = 2.0f * kPi;
    const float t = static_cast<float>(time);
    float s = 0.6f * std::sin(w * base * t) + 0.4f * std::sin(w * fifth * t);
    const float trem = 0.75f + 0.25f * std::sin(w * tremHz * t);
    return s * trem;
}

inline float clamp1(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }

inline std::int16_t toS16(float x) {
    return static_cast<std::int16_t>(clamp1(x) * 32767.0f);
}

// The concrete SDL mixer.
class SdlAudio final : public Audio {
public:
    // Try to open the audio device and start the stream. Returns false if no
    // device is available (the factory then falls back to NullAudio).
    bool open() {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
        audioSub_ = true;

        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_S16;  // 16-bit, per specs.md
        spec.channels = kChannels;
        spec.freq = kSampleRate;

        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                            &SdlAudio::feed, this);
        if (!stream_) return false;

        mutex_ = SDL_CreateMutex();
        if (!mutex_) return false;

        SDL_ResumeAudioStreamDevice(stream_);  // streams open paused
        return true;
    }

    ~SdlAudio() override {
        if (stream_) SDL_DestroyAudioStream(stream_);  // also closes the device
        if (mutex_) SDL_DestroyMutex(mutex_);
        if (audioSub_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    void playSfx(Sfx sfx) override {
        if (!mutex_) return;
        const SfxParams p = paramsFor(sfx);
        SDL_LockMutex(mutex_);
        Voice& v = pickVoice();
        v.active = true;
        v.wave = p.wave;
        v.phase = 0.0f;
        v.freq = p.f0;
        v.freqEnd = p.f1;
        v.t = 0.0f;
        v.dur = p.dur;
        v.amp = p.amp;
        v.attack = p.attack;
        v.pan = 0.0f;
        SDL_UnlockMutex(mutex_);
    }

    void setMood(Mood mood) override {
        if (!mutex_) return;
        SDL_LockMutex(mutex_);
        if (mood != curMood_) {
            // Start a fresh crossfade from whatever is currently sounding.
            prevMood_ = curMood_;
            curMood_ = mood;
            fadeP_ = 0.0f;
        }
        SDL_UnlockMutex(mutex_);
    }

private:
    // SDL pull callback: synthesize `additional` bytes on demand.
    static void SDLCALL feed(void* userdata, SDL_AudioStream* stream, int additional,
                             int /*total*/) {
        if (additional > 0) static_cast<SdlAudio*>(userdata)->generate(stream, additional);
    }

    void generate(SDL_AudioStream* stream, int bytes) {
        constexpr int kFrameBytes = kChannels * static_cast<int>(sizeof(std::int16_t));
        constexpr int kChunk = 512;  // frames per put
        std::int16_t buf[kChunk * kChannels];

        int framesLeft = bytes / kFrameBytes;
        SDL_LockMutex(mutex_);
        while (framesLeft > 0) {
            const int frames = framesLeft < kChunk ? framesLeft : kChunk;
            for (int i = 0; i < frames; ++i) {
                float l = 0.0f, r = 0.0f;
                mixFrame(l, r);
                buf[i * 2 + 0] = toS16(l);
                buf[i * 2 + 1] = toS16(r);
            }
            SDL_PutAudioStreamData(stream, buf, frames * kFrameBytes);
            framesLeft -= frames;
        }
        SDL_UnlockMutex(mutex_);
    }

    // Advance the whole mix by one sample frame. Caller holds the mutex.
    void mixFrame(float& outL, float& outR) {
        constexpr float dt = 1.0f / kSampleRate;

        // --- mood pad (crossfaded) ---
        if (fadeP_ < 1.0f) {
            fadeP_ += dt / kMoodFade;
            if (fadeP_ > 1.0f) fadeP_ = 1.0f;
        }
        const float cur = moodTone(curMood_, moodTime_);
        const float prev = moodTone(prevMood_, moodTime_);
        const float mood = (prev * (1.0f - fadeP_) + cur * fadeP_) * kMoodGain;
        moodTime_ += dt;

        float sumL = mood;
        float sumR = mood;

        // --- SFX voices ---
        for (Voice& v : voices_) {
            if (!v.active) continue;
            const float s = renderVoice(v, dt);
            const float lg = v.pan <= 0.0f ? 1.0f : 1.0f - v.pan;
            const float rg = v.pan >= 0.0f ? 1.0f : 1.0f + v.pan;
            sumL += s * lg;
            sumR += s * rg;
        }

        outL = clamp1(sumL);
        outR = clamp1(sumR);
    }

    // Render + advance a single voice. Caller holds the mutex.
    float renderVoice(Voice& v, float dt) {
        // Attack ramp, then a squared (snappy) linear decay to zero at `dur`.
        float env;
        if (v.t < v.attack) {
            env = v.attack > 0.0f ? v.t / v.attack : 1.0f;
        } else {
            const float span = v.dur - v.attack;
            const float d = span > 0.0f ? (v.t - v.attack) / span : 1.0f;
            env = d >= 1.0f ? 0.0f : (1.0f - d);
            env *= env;
        }

        // Linear frequency sweep across the voice's lifetime.
        const float k = v.dur > 0.0f ? v.t / v.dur : 1.0f;
        const float f = v.freq + (v.freqEnd - v.freq) * k;

        float sample;
        switch (v.wave) {
            case Wave::Sine:
                sample = std::sin(v.phase);
                break;
            case Wave::Square:
                sample = std::sin(v.phase) >= 0.0f ? 1.0f : -1.0f;
                break;
            case Wave::Triangle: {
                float ph = v.phase / (2.0f * kPi);
                ph -= std::floor(ph);
                sample = 4.0f * std::fabs(ph - 0.5f) - 1.0f;
                break;
            }
            case Wave::Noise:
            default:
                sample = v.noise.next();
                break;
        }

        v.phase += 2.0f * kPi * f * dt;
        if (v.phase > 2.0f * kPi * 1024.0f) {
            v.phase = std::fmod(v.phase, 2.0f * kPi);  // keep phase bounded
        }
        v.t += dt;
        if (v.t >= v.dur) v.active = false;

        return sample * env * v.amp;
    }

    // Grab a free voice, or steal the one furthest through its envelope. Caller
    // holds the mutex.
    Voice& pickVoice() {
        int oldest = 0;
        float best = -1.0f;
        for (int i = 0; i < kVoices; ++i) {
            if (!voices_[i].active) return voices_[i];
            const float progress = voices_[i].dur > 0.0f ? voices_[i].t / voices_[i].dur : 1.0f;
            if (progress > best) {
                best = progress;
                oldest = i;
            }
        }
        return voices_[oldest];
    }

    SDL_AudioStream* stream_ = nullptr;
    SDL_Mutex* mutex_ = nullptr;
    bool audioSub_ = false;

    Voice voices_[kVoices];
    Mood curMood_ = Mood::Silent;
    Mood prevMood_ = Mood::Silent;
    float fadeP_ = 1.0f;      // 0 = all prev, 1 = all cur
    double moodTime_ = 0.0;   // running pad phase clock (seconds)
};

}  // namespace

Audio* makeSdlAudio() {
    SdlAudio* a = new SdlAudio();
    if (a->open()) return a;
    // No device (or setup failed): fall back to a silent, always-safe no-op.
    delete a;
    return new NullAudio();
}

}  // namespace rv_3dmppc
