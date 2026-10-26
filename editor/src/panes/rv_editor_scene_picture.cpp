// The Scene viewport's picture: one z-buffer and one SDL texture per viewport, keyed by its ImGui ID.
// Made, reported and released here; rv_editor_scene_raster.cpp draws into it.

#include "panes/rv_editor_scene_draw.hpp"

#include <cstdio>
#include <unordered_map>

#include <SDL3/SDL.h>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "panes/rv_editor_zbuf.hpp"

namespace rv_editor
{

namespace
{

// One picture per Scene viewport, keyed by its ImGui ID. An entry, and its texture, stays until
// the editor exits: a closed tile's picture is not released (ponytail: release on tile close).
std::unordered_map<ImGuiID, rv_editor_scene_target> rv_editor_targets;

} // namespace

rv_editor_scene_target &rv_editor_scene_target_for(ImGuiID id)
{
    return rv_editor_targets[id];
}

void rv_editor_target_report(rv_editor_scene_target &t, int w, int h, const char *reason)
{
    if (t.reported) {
        return;
    }
    t.reported = true;
    std::fprintf(stderr, "3dmppc-editor: scene picture %dx%d: %s\n", w, h, reason);
}

int rv_editor_target_make(rv_editor_scene_target &t, SDL_Renderer *renderer, int w, int h)
{
    if (t.texture != nullptr && t.w == w && t.h == h) {
        return RV_OK;
    }
    if (t.texture != nullptr) {
        SDL_DestroyTexture(t.texture);
    }
    const bool reported = t.reported;
    t = {};
    t.reported = reported;
    if (rv_editor_zbuf_init(t.buf, w, h) != RV_OK) {
        rv_editor_target_report(t, w, h, "the z-buffer cannot be sized");
        return RV_ERR_INVAL;
    }
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (texture == nullptr) {
        rv_editor_target_report(t, w, h, SDL_GetError());
        return RV_ERR_IO;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    t.w = w;
    t.h = h;
    t.texture = texture;
    return RV_OK;
}

void rv_editor_scene_pictures_free()
{
    for (auto &entry : rv_editor_targets) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    rv_editor_targets.clear();
}

} // namespace rv_editor
