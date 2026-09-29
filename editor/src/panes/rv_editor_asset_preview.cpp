// The Assets preview strip: a large look at one selected file. Playback for
// sounds is the next slice; for now this only reports their shape and length.

#include "panes/rv_editor_asset_preview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

#include "imgui.h"

namespace rv_editor
{

namespace
{

// Seconds a WAV's header promises: its "fmt " chunk gives the rate and
// frame size, its "data" chunk the byte count. 0 when the file is not a WAV
// or its "fmt " chunk is too short to trust. Chunks pad to an even size, so
// an odd chunk has one skipped byte after it before the next header.
double rv_editor_wav_seconds(const std::filesystem::path &path)
{
    std::ifstream f(path, std::ios::binary);
    char riff[4] = {};
    char wave[4] = {};
    f.read(riff, 4);
    f.seekg(8, std::ios::beg);
    f.read(wave, 4);
    if (!f || std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") {
        return 0.0;
    }
    f.seekg(12, std::ios::beg);
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t rate = 0;
    uint32_t data_size = 0;
    while (f) {
        char id[4] = {};
        uint32_t size = 0;
        f.read(id, 4);
        f.read(reinterpret_cast<char *>(&size), 4);
        if (!f) {
            break;
        }
        const auto pad = static_cast<std::streamoff>(size & 1u);
        if (std::string(id, 4) == "fmt ") {
            if (size < 16) {
                f.seekg(static_cast<std::streamoff>(size) + pad, std::ios::cur);
                continue; // too short to trust: rate stays 0, duration unknown
            }
            f.seekg(2, std::ios::cur); // format tag
            f.read(reinterpret_cast<char *>(&channels), 2);
            f.read(reinterpret_cast<char *>(&rate), 4);
            f.seekg(6, std::ios::cur); // byte rate, block align
            f.read(reinterpret_cast<char *>(&bits), 2);
            f.seekg(static_cast<std::streamoff>(size) - 16 + pad, std::ios::cur);
        } else if (std::string(id, 4) == "data") {
            data_size = size;
            break;
        } else {
            f.seekg(static_cast<std::streamoff>(size) + pad, std::ios::cur);
        }
    }
    if (rate == 0 || channels == 0 || bits < 8) {
        return 0.0;
    }
    return static_cast<double>(data_size) / (rate * channels * (bits / 8));
}

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

} // namespace

double rv_editor_asset_sound_seconds(const rv_editor_asset &a)
{
    const std::string ext = a.path.extension().string();
    if (ext == ".wav") {
        return rv_editor_wav_seconds(a.path);
    }
    if (ext == ".pcm") {
        return static_cast<double>(a.size) / 88200.0;
    }
    return 0.0;
}

void rv_editor_asset_preview(const rv_editor_asset *a, const rv_editor_map_entry *entry, rv_editor_icon picture,
    double sound_seconds)
{
    if (a == nullptr) {
        ImGui::TextWrapped("Click an asset to see it here.");
        return;
    }
    const std::string ext = a->path.extension().string();
    const bool has_picture = ext == ".png" && picture.id != ImTextureID{};

    // Name and path: wraps to whatever width it is given.
    const auto header = [&]() {
        ImGui::TextWrapped("%s", a->path.filename().string().c_str());
        ImGui::TextWrapped("%s", a->rel.c_str());
        ImGui::Separator();
    };
    // What the file is, and what the build map says it became on the disc.
    const auto footer = [&]() {
        if (ext == ".png") {
            ImGui::Text(has_picture ? "%d x %d px" : "(could not load the picture)", picture.w, picture.h);
        } else if (ext == ".wav" || ext == ".pcm") {
            ImGui::Text("%.2f s", sound_seconds);
        } else {
            ImGui::TextWrapped("%s, %ju bytes",
                entry != nullptr ? entry->kind.c_str() : ext.empty() ? "file" : ext.c_str() + 1, a->size);
        }
        if (entry != nullptr) {
            ImGui::TextWrapped("On the disc: %s (%s%s%s)", entry->name.c_str(), entry->kind.c_str(),
                entry->parameter.empty() ? "" : ", ", entry->parameter.c_str());
        } else {
            ImGui::TextWrapped("Not on the disc of the last build.");
        }
    };

    if (!has_picture) {
        header();
        footer();
        return;
    }
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    if (room.y < line_h * 7.0f) {
        // A short tile: the picture on the left, all the text beside it, not under it.
        const float img_w = room.x * 0.5f - ImGui::GetStyle().ItemSpacing.x;
        ImGui::Image(picture.id, rv_editor_fit_picture(picture, img_w, room.y));
        ImGui::SameLine();
        ImGui::BeginGroup();
        header();
        footer();
        ImGui::EndGroup();
        return;
    }
    header();
    ImGui::Image(picture.id, rv_editor_fit_picture(picture, room.x, room.y - line_h * 3.0f));
    footer();
}

} // namespace rv_editor
