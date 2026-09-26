#include "ui/rv_editor_icons.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
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
// editor/icons by file stem, "folder-16".
std::map<std::string, rv_editor_icon, std::less<>> rv_editor_own_icons;

rv_editor_icon rv_editor_icon_load(SDL_Renderer *renderer, const std::string &path)
{
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
        rv_editor_icons[i] = rv_editor_icon_load(renderer, std::string(RV_EDITOR_ICON_DIR) + "/" + rv_editor_icon_files[i]);
    }
    std::error_code ec;
    for (std::filesystem::directory_iterator it(RV_EDITOR_OWN_ICON_DIR, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".png") {
            rv_editor_own_icons[it->path().stem().string()] = rv_editor_icon_load(renderer, it->path().string());
        }
    }
    if (rv_editor_own_icons.empty()) {
        std::fprintf(stderr, "3dmppc-editor: no icons in %s\n", RV_EDITOR_OWN_ICON_DIR);
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
    for (auto &[name, icon] : rv_editor_own_icons) {
        if (icon.id != 0) {
            SDL_DestroyTexture(reinterpret_cast<SDL_Texture *>(static_cast<intptr_t>(icon.id)));
        }
    }
    rv_editor_own_icons.clear();
}

rv_editor_icon rv_editor_icon_find(std::string_view name, int px)
{
    constexpr int sizes[] = { 48, 32, 24, 16 };
    for (const int size : sizes) {
        if (size > px && size != 16) {
            continue;
        }
        const auto it = rv_editor_own_icons.find(std::string(name) + "-" + std::to_string(size));
        if (it != rv_editor_own_icons.end()) {
            return it->second;
        }
    }
    return {};
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
