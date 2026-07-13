// Console audio service — the SPU-like capability the console exposes to a disc.
//
// SHARED INTERFACE — owned by the orchestrator (stable contract). The concrete
// implementation (an SDL3 mixer that degrades to a silent no-op when no audio
// device is available) is Agent A's, in core/audio.cpp. Game systems only ever
// see this abstract interface via GameContext::audio.
//
// Per docs: sample-based ADPCM-ish, 24 voices, stereo; three music moods swapped
// by location plus a tight SFX set (see art-and-audio.md).
#pragma once

namespace rv_3dmppc {

class Audio {
public:
    // Location-driven music state (crossfaded by the implementation).
    enum class Mood { Silent, Home, Street, Factory, Boss, Victory, Defeat };

    // The tight core SFX set from art-and-audio.md.
    enum class Sfx {
        Footstep,
        BrickThrow,
        BrickImpact,
        PipeSwing,
        PipeImpact,
        EnemyHurt,
        AoeWarn,
        RitualStep,
        RitualComplete,
        PlayerHurt,
        UiClick,
        Count
    };

    virtual ~Audio() = default;

    // Fire a one-shot sound effect (best-effort; drops if all voices are busy).
    virtual void playSfx(Sfx sfx) = 0;

    // Request a music mood; the implementation crossfades toward it.
    virtual void setMood(Mood mood) = 0;
};

// A no-op Audio used when no device is present or in headless smoke tests. Agent
// A may replace/extend, but this keeps the scaffold linkable on its own.
class NullAudio final : public Audio {
public:
    void playSfx(Sfx) override {}
    void setMood(Mood) override {}
};

}  // namespace rv_3dmppc
