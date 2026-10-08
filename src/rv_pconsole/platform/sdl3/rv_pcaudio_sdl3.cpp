// The SDL3 audio sink. PUSH model: the platform never advances audio, it only
// queues what the console already produced onto a device-bound stream. No
// callback and no mixer pointer live here - that is the opposite direction
// from the old design, which pulled from the mixer on the device's own
// thread.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

rv_pcaudio_sdl3::~rv_pcaudio_sdl3()
{
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
    }
}

int rv_pcaudio_sdl3::open()
{
    // The console's own format, stated once. SDL_OpenAudioDeviceStream binds a
    // converting stream to the device, so whatever rate and layout the
    // hardware actually wants is SDL's problem: the console always produces
    // RV_PCPLATFORM_PCM_RATE/CHANNELS and never learns otherwise.
    //
    // SDL_AUDIO_S16 is the NATIVE-endian alias on purpose: these are int16
    // values this process computed, not bytes read off a disc.
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = static_cast<int>(RV_PCPLATFORM_PCM_CHANNELS);
    spec.freq = static_cast<int>(RV_PCPLATFORM_PCM_RATE);

    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        RV_LOG_ERR("pcplatform", "SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        RV_LOG_ERR("pcplatform", "SDL_ResumeAudioStreamDevice failed: {}", SDL_GetError());
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        return RV_ERR_IO;
    }

    RV_LOG_INFO("pcplatform", "audio out at {} Hz, {} ch", RV_PCPLATFORM_PCM_RATE, RV_PCPLATFORM_PCM_CHANNELS);
    return RV_OK;
}

bool rv_pcaudio_sdl3::available() const
{
    return stream_ != nullptr;
}

int64_t rv_pcaudio_sdl3::queued_frames() const
{
    if (!stream_) {
        return 0;
    }

    const int queued_bytes = SDL_GetAudioStreamQueued(stream_);
    const int frame_bytes = static_cast<int>(RV_PCPLATFORM_PCM_CHANNELS) * static_cast<int>(sizeof(int16_t));
    return queued_bytes / frame_bytes;
}

void rv_pcaudio_sdl3::write(const int16_t *interleaved, int64_t frames)
{
    if (!stream_ || !interleaved || frames <= 0) {
        return;
    }

    const int bytes =
        static_cast<int>(frames) * static_cast<int>(RV_PCPLATFORM_PCM_CHANNELS) * static_cast<int>(sizeof(int16_t));
    if (!SDL_PutAudioStreamData(stream_, interleaved, bytes) && !warned_once_) {
        // Losing one write is not worth spinning over; the caller writes
        // again next frame. Warned once rather than every frame: a failing
        // device would otherwise flood the log at the console's frame rate.
        RV_LOG_WARN("pcplatform", "SDL_PutAudioStreamData failed: {}", SDL_GetError());
        warned_once_ = true;
    }
}

} // namespace rv_3dmppc
