#pragma once

#include <string_view>

#include "imgui.h"

struct SDL_Renderer;

namespace rv_editor
{

// The win-55-ui icons (third_party/win55-icons), decoded once into renderer
// textures. They are read from the repository at run time, the way mppcburner
// finds pdk/: RV_EDITOR_ICON_DIR is baked in by editor/CMakeLists.txt.

enum class rv_editor_icon_name
{
    broken_image,
    calendar,
    folder,
    gizmo,
    neko,
    program,
    count,
};

struct rv_editor_icon
{
    ImTextureID id; // 0 when the file could not be loaded
    int w;
    int h;
};

// Loads every icon; a file that fails is reported on stderr and left empty.
void rv_editor_icons_load(SDL_Renderer *renderer);
void rv_editor_icons_free();

rv_editor_icon rv_editor_icon_get(rv_editor_icon_name name);

// Any PNG as a texture on `renderer`, drawn with nearest sampling (Assets' pictures);
// an empty icon when it does not load. The caller keeps it for the session.
rv_editor_icon rv_editor_image_load(SDL_Renderer *renderer, const std::string &path);

// The editor's own icons (editor/icons, spec section 16) by name and pixel size,
// "folder" at 16: the largest drawn size not above `px` (16, 24, 32, 48), or the
// smallest when `px` is below all. An unknown name gives an empty icon.
rv_editor_icon rv_editor_icon_find(std::string_view name, int px);

// Whole-number size multiplier for the icons at a UI scale: they are drawn for
// a 1x desktop, which is the editor's scale 1 (its 16 px font).
int rv_editor_icon_scale(float ui_scale);

} // namespace rv_editor
