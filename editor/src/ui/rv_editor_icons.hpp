#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "imgui.h"
#include "pdk/rv_err.h"

struct SDL_Renderer;

namespace rv_editor
{

// The win-55-ui icons (third_party/win55-icons), decoded once into renderer
// textures. They are read from the repository at run time, the way mppcburner
// finds pdk/: RV_EDITOR_ICON_DIR is baked in by editor/CMakeLists.txt.

enum class rv_editor_icon_name {
    broken_image,
    calendar,
    folder,
    gizmo,
    neko,
    program,
    count,
};

struct rv_editor_icon {
    ImTextureID id; // 0 when the file could not be loaded
    int w;
    int h;
};

// A picture decoded to memory: RGBA8 rows from the top, SDL_PIXELFORMAT_RGBA32.
struct rv_editor_cpu_image {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba; // w * h * 4 bytes
};

// Loads every icon; a file that fails is reported on stderr and left empty.
void rv_editor_icons_load(SDL_Renderer *renderer);
void rv_editor_icons_free();

rv_editor_icon rv_editor_icon_get(rv_editor_icon_name name);

// Any PNG as a texture on `renderer`, drawn with nearest sampling (Assets' pictures);
// an empty icon when it does not load. The caller keeps it for the session.
rv_editor_icon rv_editor_image_load(SDL_Renderer *renderer, const std::string &path);

// The same for a PNG held in memory (an image embedded in a .glb).
rv_editor_icon rv_editor_image_load_png(SDL_Renderer *renderer, std::span<const uint8_t> png);

// The same pictures decoded to memory, without a renderer. On failure the reason goes to
// stderr and `image` is untouched: RV_ERR_NOENT when the file cannot be opened, RV_ERR_IO
// when reading it fails, RV_ERR_INVAL for any decode failure (not a PNG, over INT_MAX
// bytes, or refused by stb). Peak memory during a load is about three picture sizes:
// file bytes, the stb buffer and the RGBA copy.
int rv_editor_cpu_image_load(const std::string &path, rv_editor_cpu_image &image);
int rv_editor_cpu_image_load_png(std::span<const uint8_t> png, rv_editor_cpu_image &image);

// Whole-number size multiplier for the icons at a UI scale: they are drawn for
// a 1x desktop, which is the editor's scale 1 (its 16 px font).
int rv_editor_icon_scale(float ui_scale);

} // namespace rv_editor
