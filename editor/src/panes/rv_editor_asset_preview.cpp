// The Assets preview strip: a large look at one selected file, with a
// Play/Stop pair for sounds.

#include "panes/rv_editor_asset_preview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string_view>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_sound.hpp"

namespace rv_editor
{

namespace
{

// The last failed play, kept until the selection moves to a different file.
struct rv_editor_sound_error {
    std::filesystem::path path;
    std::string text;
};

rv_editor_sound_error rv_editor_sound_last_error;

// The last failed "Add to disc", kept until the selection moves to a different file.
struct rv_editor_add_error {
    std::string rel;
    std::string text;
};

rv_editor_add_error rv_editor_add_last_error;

// Code file extensions excluded from asset sections.
constexpr std::string_view code_file_extensions[] = { ".cpp", ".hpp", ".h", ".c", ".lua" };

// Disc manifest file name.
constexpr std::string_view disc_manifest_file = "disc.toml";

// Scene definition file extension.
constexpr std::string_view scene_file_extension = ".scene.toml";

// Picture asset file extension.
constexpr std::string_view picture_extension = ".png";

// Audio file extension: WAV format.
constexpr std::string_view wav_extension = ".wav";

// Audio file extension: raw PCM samples.
constexpr std::string_view pcm_extension = ".pcm";

// PCM sample rate in Hz.
constexpr double pcm_sample_rate = 88200.0;

// Position of "WAVE" format marker in WAV file header.
constexpr std::streamoff wav_wave_marker_offset = 8;

// Position where chunk list begins in WAV file.
constexpr std::streamoff wav_chunks_start_offset = 12;

// Minimum size of WAV "fmt " chunk in bytes.
constexpr uint32_t wav_fmt_chunk_min_size = 16;

// Size of byte rate and block align fields in WAV "fmt " chunk (bytes to skip).
constexpr std::streamoff wav_byte_rate_block_align_size = 6;

// Minimum bits per sample for valid WAV audio data.
constexpr int wav_min_bits_per_sample = 8;

// Size of chunk identifiers in RIFF/WAVE format: RIFF, WAVE, fmt, data tags.
constexpr size_t wav_riff_chunk_id_bytes = 4;

// WAV/RIFF format chunk identifiers.
constexpr std::string_view wav_riff_chunk_id = "RIFF";
constexpr std::string_view wav_wave_chunk_id = "WAVE";
constexpr std::string_view wav_fmt_chunk_id = "fmt ";
constexpr std::string_view wav_data_chunk_id = "data";

// Size of 16-bit fields in WAV fmt chunk: format tag, channels, bits per sample.
constexpr size_t wav_u16_field_bytes = 2;

// Size of 32-bit fields in RIFF/WAVE format: chunk size, sample rate.
constexpr size_t wav_u32_field_bytes = 4;

// Bits per byte for audio data conversion.
constexpr int bits_per_byte = 8;

// False for sources, scripts, disc.toml itself and scene files: none of these
// belong in a disc's asset sections.
bool rv_editor_asset_belongs_on_disc(std::string_view rel)
{
    for (std::string_view ext : code_file_extensions) {
        if (rel.ends_with(ext)) {
            return false;
        }
    }
    return rel != disc_manifest_file && !rel.ends_with(scene_file_extension);
}

// Extract fmt chunk: read channels, rate, bits; short chunks are skipped.
void rv_editor_wav_fmt_chunk(std::ifstream &f, uint32_t chunk_size, uint16_t &channels, uint16_t &bits, uint32_t &rate)
{
    if (chunk_size < wav_fmt_chunk_min_size) {
        // too short to trust: rate stays 0, duration unknown
        f.seekg(static_cast<std::streamoff>(chunk_size) + (chunk_size & 1u), std::ios::cur);
        return;
    }
    f.seekg(static_cast<std::streamoff>(wav_u16_field_bytes), std::ios::cur);
    f.read(reinterpret_cast<char *>(&channels), static_cast<std::streamsize>(wav_u16_field_bytes));
    f.read(reinterpret_cast<char *>(&rate), static_cast<std::streamsize>(wav_u32_field_bytes));
    f.seekg(wav_byte_rate_block_align_size, std::ios::cur);
    f.read(reinterpret_cast<char *>(&bits), static_cast<std::streamsize>(wav_u16_field_bytes));
    f.seekg(static_cast<std::streamoff>(chunk_size) - wav_fmt_chunk_min_size + (chunk_size & 1u), std::ios::cur);
}

// Seconds a WAV's header promises: its "fmt " chunk gives the rate and
// frame size, its "data" chunk the byte count. 0 when the file is not a WAV
// or its "fmt " chunk is too short to trust. Chunks pad to an even size, so
// an odd chunk has one skipped byte after it before the next header.
double rv_editor_wav_seconds(const std::filesystem::path &path)
{
    std::ifstream f(path, std::ios::binary);
    char riff[wav_riff_chunk_id_bytes] = {};
    char wave[wav_riff_chunk_id_bytes] = {};
    f.read(riff, wav_riff_chunk_id_bytes);
    f.seekg(wav_wave_marker_offset, std::ios::beg);
    f.read(wave, static_cast<std::streamsize>(wav_riff_chunk_id_bytes));
    const bool is_riff = std::string(riff, wav_riff_chunk_id_bytes) == wav_riff_chunk_id;
    const bool is_wave = std::string(wave, wav_riff_chunk_id_bytes) == wav_wave_chunk_id;
    if (!f || !is_riff || !is_wave) {
        return 0.0;
    }
    f.seekg(wav_chunks_start_offset, std::ios::beg);
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t rate = 0;
    uint32_t data_size = 0;
    while (f) {
        char id[wav_riff_chunk_id_bytes] = {};
        uint32_t size = 0;
        f.read(id, static_cast<std::streamsize>(wav_riff_chunk_id_bytes));
        f.read(reinterpret_cast<char *>(&size), static_cast<std::streamsize>(wav_u32_field_bytes));
        if (!f) {
            break;
        }
        const std::string_view chunk_id(id, wav_riff_chunk_id_bytes);
        const auto pad = static_cast<std::streamoff>(size & 1u);
        if (chunk_id == wav_fmt_chunk_id) {
            rv_editor_wav_fmt_chunk(f, size, channels, bits, rate);
            continue;
        }
        if (chunk_id == wav_data_chunk_id) {
            data_size = size;
            break;
        }
        f.seekg(static_cast<std::streamoff>(size) + pad, std::ios::cur);
    }
    if (rate == 0 || channels == 0 || bits < wav_min_bits_per_sample) {
        return 0.0;
    }
    return static_cast<double>(data_size) / (rate * channels * (bits / bits_per_byte));
}

} // namespace

// The picture's size at the largest whole-number scale that fits the box,
// or scaled down to fit when even 1x does not.
ImVec2 rv_editor_fit_picture(rv_editor_icon picture, float box_w, float box_h)
{
    box_w = std::max(1.0f, box_w);
    box_h = std::max(1.0f, box_h);
    const float k = std::min(box_w / static_cast<float>(picture.w), box_h / static_cast<float>(picture.h));
    const float scale = k >= 1.0f ? std::floor(k) : k;
    return ImVec2(picture.w * scale, picture.h * scale);
}

double rv_editor_asset_sound_seconds(const rv_editor_asset &a)
{
    const std::string ext = a.path.extension().string();
    if (ext == wav_extension) {
        return rv_editor_wav_seconds(a.path);
    }
    if (ext == pcm_extension) {
        return static_cast<double>(a.size) / pcm_sample_rate;
    }
    return 0.0;
}

rv_editor_asset_view rv_editor_asset_preview(const rv_editor_asset *a,
    const rv_editor_map_entry *entry,
    rv_editor_icon picture,
    double sound_seconds,
    const rv_editor_project &project)
{
    rv_editor_asset_view view;
    if (a == nullptr) {
        return view;
    }
    const std::string ext = a->path.extension().string();
    view.text = a->rel;
    // What the file is.
    if (ext == picture_extension) {
        const std::string failed = rv_editor_text("asset_preview.picture_load_failed");
        view.text += "\n" +
            (picture.id != ImTextureID{} ?
                    rv_editor_text_format("asset_preview.picture_size", std::make_format_args(picture.w, picture.h)) :
                    failed);
    } else if (ext == wav_extension || ext == pcm_extension) {
        view.text += "\n" + rv_editor_text_format("asset_preview.sound_duration", std::make_format_args(sound_seconds));
        view.buttons = { rv_editor_text("asset_preview.button_play"), rv_editor_text("asset_preview.button_stop") };
        view.actions = { rv_editor_asset_action::play, rv_editor_asset_action::stop };
        if (rv_editor_sound_playing() && rv_editor_sound_path() == a->path) {
            view.text += ", " + std::string(rv_editor_text("asset_preview.status_playing"));
        }
        if (rv_editor_sound_last_error.path == a->path) {
            view.text += ": " + rv_editor_sound_last_error.text;
        }
    } else {
        const char *kind;
        if (entry != nullptr) {
            kind = entry->kind.c_str();
        } else if (ext.empty()) {
            kind = rv_editor_text("asset_preview.file_kind_fallback");
        } else {
            kind = ext.c_str() + 1;
        }
        view.text += "\n" + rv_editor_text_format("asset_preview.file_info", std::make_format_args(kind, a->size));
    }
    // What the build map says it became on the disc.
    if (entry != nullptr) {
        if (entry->parameter.empty()) {
            view.text += ". " + rv_editor_text_format("asset_preview.on_disc", std::make_format_args(entry->name, entry->kind));
        } else {
            view.text += ". " +
                rv_editor_text_format("asset_preview.on_disc_with_parameter",
                    std::make_format_args(entry->name, entry->kind, entry->parameter));
        }
    } else {
        view.text += ". " + std::string(rv_editor_text("asset_preview.not_on_disc_last_build"));
    }
    if (!rv_editor_project_on_disc(project, a->rel) && rv_editor_asset_belongs_on_disc(a->rel)) {
        view.text += " " + std::string(rv_editor_text("asset_preview.not_in_disc_toml"));
        view.buttons.emplace_back(rv_editor_text("asset_preview.button_add_to_disc"));
        view.actions.push_back(rv_editor_asset_action::add_to_disc);
        if (rv_editor_add_last_error.rel == a->rel) {
            view.text += ": " + rv_editor_add_last_error.text;
        }
    }
    return view;
}

void rv_editor_asset_act(rv_editor_asset_action action, const rv_editor_asset &a, rv_editor_project &project)
{
    std::string error;
    switch (action) {
    case rv_editor_asset_action::play:
        rv_editor_sound_last_error =
            rv_editor_sound_play(a.path, error) == RV_OK ? rv_editor_sound_error{} : rv_editor_sound_error{ a.path, error };
        break;
    case rv_editor_asset_action::stop:
        rv_editor_sound_stop();
        break;
    case rv_editor_asset_action::add_to_disc:
        rv_editor_add_last_error = rv_editor_project_put_on_disc(project, a.rel, error) == RV_OK ?
            rv_editor_add_error{} :
            rv_editor_add_error{ a.rel, error };
        break;
    }
}

void rv_editor_asset_picture_show(rv_editor_icon picture, float max_width, float max_height)
{
    if (picture.id != ImTextureID{}) {
        ImGui::Image(picture.id, rv_editor_fit_picture(picture, max_width, max_height));
    }
}

} // namespace rv_editor
