#include "ui/rv_editor_sound.hpp"

#include <fstream>
#include <vector>

#include <SDL3/SDL.h>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Sample rate of the interface sounds in Hz.
constexpr int sound_sample_rate = 44100;

// One sound at a time: the open audio subsystem, its stream, and the file it plays.
struct rv_editor_sound_state
{
    bool audio_ready = false;
    SDL_AudioStream *stream = nullptr;
    std::filesystem::path path;
};

rv_editor_sound_state rv_editor_sound;

// The console's raw sound: S16LE mono 44100 Hz, no header.
std::vector<uint8_t> rv_editor_pcm_read(const std::filesystem::path &file, std::string &error)
{
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        error = "cannot open the file";
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

} // namespace

int rv_editor_sound_play(const std::filesystem::path &file, std::string &error)
{
    rv_editor_sound_stop();

    if (!rv_editor_sound.audio_ready) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            error = SDL_GetError();
            return RV_ERR_IO;
        }
        rv_editor_sound.audio_ready = true;
    }

    SDL_AudioSpec spec{};
    uint8_t *wav_buf = nullptr;
    uint32_t wav_len = 0;
    std::vector<uint8_t> pcm;
    const uint8_t *data = nullptr;
    uint32_t len = 0;

    if (file.extension() == ".wav") {
        if (!SDL_LoadWAV(file.string().c_str(), &spec, &wav_buf, &wav_len)) {
            error = SDL_GetError();
            return RV_ERR_INVAL;
        }
        data = wav_buf;
        len = wav_len;
    } else if (file.extension() == ".pcm") {
        pcm = rv_editor_pcm_read(file, error);
        if (pcm.empty() && !error.empty()) {
            return RV_ERR_IO;
        }
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = 1;
        spec.freq = sound_sample_rate;
        data = pcm.data();
        len = static_cast<uint32_t>(pcm.size());
    } else {
        error = "not a sound file";
        return RV_ERR_INVAL;
    }

    rv_editor_sound.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (rv_editor_sound.stream == nullptr) {
        error = SDL_GetError();
        SDL_free(wav_buf);
        return RV_ERR_IO;
    }
    const bool put = SDL_PutAudioStreamData(rv_editor_sound.stream, data, static_cast<int>(len));
    SDL_free(wav_buf);
    if (!put || !SDL_ResumeAudioStreamDevice(rv_editor_sound.stream)) {
        error = SDL_GetError();
        SDL_DestroyAudioStream(rv_editor_sound.stream);
        rv_editor_sound.stream = nullptr;
        return RV_ERR_IO;
    }
    rv_editor_sound.path = file;
    return RV_OK;
}

void rv_editor_sound_stop()
{
    if (rv_editor_sound.stream == nullptr) {
        return;
    }
    SDL_DestroyAudioStream(rv_editor_sound.stream);
    rv_editor_sound.stream = nullptr;
    rv_editor_sound.path.clear();
}

bool rv_editor_sound_playing()
{
    if (rv_editor_sound.stream == nullptr) {
        return false;
    }
    if (SDL_GetAudioStreamAvailable(rv_editor_sound.stream) > 0 ||
        SDL_GetAudioStreamQueued(rv_editor_sound.stream) > 0) {
        return true;
    }
    rv_editor_sound_stop();
    return false;
}

const std::filesystem::path &rv_editor_sound_path()
{
    return rv_editor_sound.path;
}

void rv_editor_sound_shutdown()
{
    rv_editor_sound_stop();
    if (rv_editor_sound.audio_ready) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        rv_editor_sound.audio_ready = false;
    }
}

} // namespace rv_editor
