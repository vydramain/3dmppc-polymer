#include "ui/rv_editor_icons.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Icons load as 8-bit RGBA: four bytes per pixel.
constexpr int rgba_channels = 4;

constexpr int rv_editor_icon_count = static_cast<int>(rv_editor_icon_name::count);

constexpr const char *rv_editor_icon_files[rv_editor_icon_count] = {
    "broken-image.png",
    "calendar.png",
    "folder.png",
    "gizmo.png",
    "neko.png",
    "program.png",
};

rv_editor_icon rv_editor_icons[rv_editor_icon_count] = {};

// Uploads decoded RGBA pixels; `source` names the image in the failure message.
rv_editor_icon rv_editor_icon_texture(SDL_Renderer *renderer, const rv_editor_cpu_image &image, const std::string &source)
{
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.w, image.h);
    if (texture == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: icon %s: %s\n", source.c_str(), SDL_GetError());
        return {};
    }
    SDL_UpdateTexture(texture, nullptr, image.rgba.data(), image.w * rgba_channels);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return { static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture)), image.w, image.h };
}

// The one decoder: every loader above reads bytes and comes here. `source` names the image in messages.
int rv_editor_cpu_image_decode(std::span<const uint8_t> png, const std::string &source, rv_editor_cpu_image &image)
{
    if (png.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        std::fprintf(stderr, "3dmppc-editor: icon %s: too large\n", source.c_str());
        return RV_ERR_INVAL;
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    const int size = static_cast<int>(png.size());
    unsigned char *rgba = stbi_load_from_memory(png.data(), size, &w, &h, &channels, rgba_channels);
    if (rgba == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: icon %s: %s\n", source.c_str(), stbi_failure_reason());
        return RV_ERR_INVAL;
    }

    image.w = w;
    image.h = h;
    image.rgba.assign(rgba, rgba + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * rgba_channels);
    stbi_image_free(rgba);
    return RV_OK;
}

} // namespace

int rv_editor_cpu_image_load(const std::string &path, rv_editor_cpu_image &image)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "3dmppc-editor: icon %s: cannot open\n", path.c_str());
        return RV_ERR_NOENT;
    }

    // istreambuf_iterator does not report read errors, so size the buffer and read it whole.
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 0) {
        std::fprintf(stderr, "3dmppc-editor: icon %s: read failed\n", path.c_str());
        return RV_ERR_IO;
    }
    if (size > std::numeric_limits<int>::max()) {
        std::fprintf(stderr, "3dmppc-editor: icon %s: too large\n", path.c_str());
        return RV_ERR_INVAL;
    }

    std::vector<uint8_t> png(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char *>(png.data()), size);
    if (!in) {
        std::fprintf(stderr, "3dmppc-editor: icon %s: read failed\n", path.c_str());
        return RV_ERR_IO;
    }
    return rv_editor_cpu_image_decode(png, path, image);
}

int rv_editor_cpu_image_load_png(std::span<const uint8_t> png, rv_editor_cpu_image &image)
{
    return rv_editor_cpu_image_decode(png, "embedded PNG", image);
}

rv_editor_icon rv_editor_image_load(SDL_Renderer *renderer, const std::string &path)
{
    rv_editor_cpu_image image;
    if (rv_editor_cpu_image_load(path, image) != RV_OK) {
        return {};
    }
    return rv_editor_icon_texture(renderer, image, path);
}

rv_editor_icon rv_editor_image_load_png(SDL_Renderer *renderer, std::span<const uint8_t> png)
{
    rv_editor_cpu_image image;
    if (rv_editor_cpu_image_load_png(png, image) != RV_OK) {
        return {};
    }
    return rv_editor_icon_texture(renderer, image, "embedded PNG");
}

void rv_editor_icons_load(SDL_Renderer *renderer)
{
    for (int i = 0; i < rv_editor_icon_count; ++i) {
        rv_editor_icons[i] = rv_editor_image_load(renderer, std::string(RV_EDITOR_ICON_DIR) + "/" + rv_editor_icon_files[i]);
    }
}

void rv_editor_icons_free()
{
    for (rv_editor_icon &icon : rv_editor_icons) {
        if (icon.id != 0) {
            SDL_DestroyTexture(reinterpret_cast<SDL_Texture *>(static_cast<intptr_t>(icon.id)));
        }
        icon = {};
    }
}

rv_editor_icon rv_editor_icon_get(rv_editor_icon_name name)
{
    return rv_editor_icons[static_cast<int>(name)];
}

int rv_editor_icon_scale(float ui_scale)
{
    // Bitmaps grow by whole pixels only: 1.5 draws them at 1x, 2.5 at 2x.
    return std::max(1, static_cast<int>(ui_scale));
}

} // namespace rv_editor
