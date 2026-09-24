#pragma once

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

// Whole-number size multiplier for the icons at a UI scale: they are drawn for
// a 1x desktop, which is the editor's scale 1 (its 16 px font).
int rv_editor_icon_scale(int ui_scale);

} // namespace rv_editor
