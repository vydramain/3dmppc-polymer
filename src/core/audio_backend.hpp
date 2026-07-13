// Factory for the SDL3-backed audio mixer. Declared here so the console
// (platform/console.cpp) can spin up real audio without depending on the
// concrete mixer type — the implementation lives in core/audio.cpp (Agent A).
#pragma once

namespace rv_3dmppc {

class Audio;

// Create the console's audio device: an SDL3 mixer (procedural SFX + crossfaded
// mood pads, 16-bit stereo). The caller owns the returned object and must
// `delete` it. Never returns null — if no audio device can be opened it returns
// a silent NullAudio, so callers always get a usable, non-null Audio.
Audio* makeSdlAudio();

}  // namespace rv_3dmppc
