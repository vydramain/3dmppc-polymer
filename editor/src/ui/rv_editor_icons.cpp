#include "ui/rv_editor_icons.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace rv_editor
{

namespace
{

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

rv_editor_icon rv_editor_icon_load(SDL_Renderer *renderer, const char *file)
{
    const std::string path = std::string(RV_EDITOR_ICON_DIR) + "/" + file;
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *rgba = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (rgba == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: icon %s: %s\n", path.c_str(), stbi_failure_reason());
        return {};
    }

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (texture == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: icon %s: %s\n", path.c_str(), SDL_GetError());
        stbi_image_free(rgba);
        return {};
    }
    SDL_UpdateTexture(texture, nullptr, rgba, w * 4);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    stbi_image_free(rgba);
    return {static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture)), w, h};
}

} // namespace

void rv_editor_icons_load(SDL_Renderer *renderer)
{
    for (int i = 0; i < rv_editor_icon_count; ++i) {
        rv_editor_icons[i] = rv_editor_icon_load(renderer, rv_editor_icon_files[i]);
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

int rv_editor_icon_scale(int ui_scale)
{
    return std::max(1, ui_scale / 2);
}

} // namespace rv_editor
