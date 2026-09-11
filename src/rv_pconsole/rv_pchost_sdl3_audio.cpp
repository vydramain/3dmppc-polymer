// rv_pchost_sdl3: the audio device and the callback SDL runs on its own thread.
#include "rv_pconsole/rv_pchost_sdl3.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/ca/rv_pcmixer.hpp"

namespace rv_3dmppc
{

namespace
{

// Frames the audio callback produces per pass. Small enough that the staging
// buffer is a few kilobytes, large enough that a device asking for 20 ms does
// not walk the mixer a hundred times.
constexpr int RV_PCHOST_AUDIO_BLOCK_FRAMES = 512;

// Bytes one stereo S16 frame occupies on the device side.
constexpr int RV_PCHOST_AUDIO_FRAME_BYTES = 2 * static_cast<int>(sizeof(int16_t));

} // namespace

bool rv_pchost_sdl3::open_audio_device()
{
    if (audio_stream_) {
        return true;
    }

    audio_block_.assign(
        static_cast<std::size_t>(RV_PCHOST_AUDIO_BLOCK_FRAMES) * RV_PCMIXER_CHANNELS, 0);

    // The console's own format, stated once. SDL_OpenAudioDeviceStream binds a
    // converting stream to the device, so whatever rate and layout the hardware
    // actually wants is SDL's problem: the mixer always produces 44100 Hz
    // stereo and never learns otherwise. That is why the SPU has no resampler —
    // see the zero-resampling theorem in rv_pcvoice.cpp.
    //
    // SDL_AUDIO_S16 is the NATIVE-endian alias on purpose: these are int16
    // values this process computed, not bytes read off a disc. The S16LE in the
    // sound-RAM format is a different statement and lives in rv_pcvoice.cpp.
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = static_cast<int>(RV_PCMIXER_CHANNELS);
    spec.freq = static_cast<int>(RV_PCA_SAMPLE_RATE);

    audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
        &rv_pchost_sdl3::audio_stream_callback, this);
    if (!audio_stream_) {
        RV_LOG_WARN("pchost", "SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
        return false;
    }

    // Devices open paused; from here on the callback runs on SDL's thread. No
    // mixer is attached yet — fill_audio() fills silence until attach_mixer()
    // is called.
    if (!SDL_ResumeAudioStreamDevice(audio_stream_)) {
        RV_LOG_WARN("pchost", "SDL_ResumeAudioStreamDevice failed: {}", SDL_GetError());
        SDL_DestroyAudioStream(audio_stream_);
        audio_stream_ = nullptr;
        return false;
    }

    RV_LOG_INFO("pchost", "audio out at {} Hz, {} ch", RV_PCA_SAMPLE_RATE, RV_PCMIXER_CHANNELS);
    return true;
}

void rv_pchost_sdl3::attach_mixer(rv_pcmixer &mixer)
{
    if (!audio_stream_) {
        return;
    }

    // Locking the stream keeps the callback, which runs on SDL's own thread,
    // from reading audio_mixer_ mid-assignment.
    SDL_LockAudioStream(audio_stream_);
    audio_mixer_ = &mixer;
    SDL_UnlockAudioStream(audio_stream_);
}

void rv_pchost_sdl3::detach_mixer()
{
    if (!audio_stream_) {
        audio_mixer_ = nullptr;
        return;
    }

    SDL_LockAudioStream(audio_stream_);
    audio_mixer_ = nullptr;
    SDL_UnlockAudioStream(audio_stream_);
}

void rv_pchost_sdl3::close_audio()
{
    if (!audio_stream_) {
        audio_mixer_ = nullptr;
        return;
    }

    // SDL_DestroyAudioStream unbinds the stream from the device and waits for a
    // callback in flight to finish, so once it returns the mixer pointer is
    // provably unreachable and rv_pcca may destroy the mixer it points at.
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
    audio_mixer_ = nullptr;
}

void rv_pchost_sdl3::audio_stream_callback(void *userdata, SDL_AudioStream *stream,
    int additional_amount, int /*total_amount*/)
{
    rv_pchost_sdl3 *host = static_cast<rv_pchost_sdl3 *>(userdata);
    if (host) {
        host->fill_audio(stream, additional_amount);
    }
}

void rv_pchost_sdl3::fill_audio(SDL_AudioStream *stream, int wanted_bytes)
{
    if (!stream || wanted_bytes <= 0) {
        return;
    }

    // `wanted_bytes` is expressed in the DEVICE's format, which after SDL's
    // conversion need not be ours; rounding up by our own frame size only ever
    // hands the stream a little more than it asked for, which it buffers.
    int frames_left =
        (wanted_bytes + RV_PCHOST_AUDIO_FRAME_BYTES - 1) / RV_PCHOST_AUDIO_FRAME_BYTES;

    while (frames_left > 0) {
        const int block =
            frames_left < RV_PCHOST_AUDIO_BLOCK_FRAMES ? frames_left : RV_PCHOST_AUDIO_BLOCK_FRAMES;

        // No mixer attached (device came up before any disc did, or the disc
        // runs with ca=null): the device stays alive and clocked, it just gets
        // silence instead of the SPU's output.
        if (audio_mixer_) {
            audio_mixer_->render(audio_block_.data(), block);
        } else {
            std::fill_n(audio_block_.data(), static_cast<std::size_t>(block) * RV_PCMIXER_CHANNELS,
                static_cast<int16_t>(0));
        }
        if (!SDL_PutAudioStreamData(stream, audio_block_.data(),
                block * RV_PCHOST_AUDIO_FRAME_BYTES)) {
            // Losing the queue mid-callback is not worth spinning over; the
            // device will ask again and the next block starts a frame later.
            return;
        }

        frames_left -= block;
    }
}

} // namespace rv_3dmppc
