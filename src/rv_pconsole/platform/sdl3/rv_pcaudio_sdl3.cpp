// The SDL3 audio sink. It only pulls from the attached source: native from
// service() on the console's loop, web from SDL's get callback on the main thread.
#include "rv_pconsole/platform/sdl3/rv_pcplatform_sdl3_detail.hpp"

#include <algorithm>

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

    const SDL_AudioStreamCallback get = RV_PCAUDIO_SDL3_CALLBACK_PULL ? &rv_pcaudio_sdl3::pull_callback : nullptr;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, get, this);
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

    // The device block size is logged for latency estimates; not needed to play.
    SDL_AudioSpec device_spec{};
    int sample_frames = 0;
    if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &device_spec, &sample_frames)) {
        RV_LOG_INFO("pcplatform",
            "audio out at {} Hz, {} ch; device {} Hz, {} sample frames per block",
            RV_PCPLATFORM_PCM_RATE,
            RV_PCPLATFORM_PCM_CHANNELS,
            device_spec.freq,
            sample_frames);
    } else {
        RV_LOG_WARN("pcplatform", "SDL_GetAudioDeviceFormat failed: {}", SDL_GetError());
        RV_LOG_INFO("pcplatform",
            "audio out at {} Hz, {} ch; device format unknown",
            RV_PCPLATFORM_PCM_RATE,
            RV_PCPLATFORM_PCM_CHANNELS);
    }
    return RV_OK;
}

bool rv_pcaudio_sdl3::available() const
{
    return stream_ != nullptr;
}

int64_t rv_pcaudio_sdl3::queued() const
{
    if (!stream_) {
        return 0;
    }

    const int queued_bytes = SDL_GetAudioStreamQueued(stream_);
    return queued_bytes / RV_PCAUDIO_SDL3_FRAME_BYTES;
}

void rv_pcaudio_sdl3::put(const int16_t *interleaved, int64_t frames)
{
    if (!stream_ || !interleaved || frames <= 0) {
        return;
    }

    const int bytes = static_cast<int>(frames) * RV_PCAUDIO_SDL3_FRAME_BYTES;
    if (!SDL_PutAudioStreamData(stream_, interleaved, bytes) && !warned_once_) {
        // Losing one write is not worth spinning over; the caller writes
        // again next frame. Warned once rather than every frame: a failing
        // device would otherwise flood the log at the console's frame rate.
        RV_LOG_WARN("pcplatform", "SDL_PutAudioStreamData failed: {}", SDL_GetError());
        warned_once_ = true;
    }
}

void rv_pcaudio_sdl3::attach_source(rv_pcaudio_fill_fn fill, void *context)
{
    fill_ = fill;
    fill_context_ = context;
}

void rv_pcaudio_sdl3::service()
{
    if (RV_PCAUDIO_SDL3_CALLBACK_PULL || !stream_ || !fill_) {
        return;
    }
    pull_frames(RV_PCAUDIO_SDL3_NATIVE_TARGET_FRAMES - queued());
}

int rv_pcaudio_sdl3::pause()
{
    if (!stream_) {
        return RV_OK;
    }
    if (!SDL_PauseAudioStreamDevice(stream_)) {
        RV_LOG_WARN("pcplatform", "SDL_PauseAudioStreamDevice failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    return RV_OK;
}

int rv_pcaudio_sdl3::resume()
{
    if (!stream_) {
        return RV_OK;
    }
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        RV_LOG_WARN("pcplatform", "SDL_ResumeAudioStreamDevice failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    return RV_OK;
}

void SDLCALL rv_pcaudio_sdl3::pull_callback(void *userdata, SDL_AudioStream *, int additional_amount, int)
{
    static_cast<rv_pcaudio_sdl3 *>(userdata)->pull_frames(additional_amount / RV_PCAUDIO_SDL3_FRAME_BYTES);
}

void rv_pcaudio_sdl3::pull_frames(int64_t frames)
{
    if (!fill_) {
        return;
    }
    while (frames > 0) {
        const int64_t chunk = std::min(frames, RV_PCAUDIO_SDL3_CHUNK_FRAMES);
        fill_(fill_context_, fill_buffer_, chunk);
        put(fill_buffer_, chunk);
        frames -= chunk;
    }
}

} // namespace rv_3dmppc
