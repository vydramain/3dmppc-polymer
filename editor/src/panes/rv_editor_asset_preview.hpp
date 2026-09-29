#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "build/rv_editor_build_map.hpp"
#include "project/rv_editor_project.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_icons.hpp"

struct SDL_Renderer;

namespace rv_editor
{

// One resource file as Assets lists it.
struct rv_editor_asset
{
    std::filesystem::path path;
    std::string rel;    // relative to the project root, as the map names it
    std::string folder; // its top-level folder
    uintmax_t size = 0;
};

// A sound's length in seconds: a WAV's header, or a raw PCM's size / 88200
// (s16le, 44100 Hz, mono). 0 when `a` is not a sound or its header is unclear.
// Opens and reads the file; the caller keeps the result and only calls this
// again when the selection or the file's write time changes.
double rv_editor_asset_sound_seconds(const rv_editor_asset &a);

// The preview strip for the selected asset: its name and path, then what it
// is - a PNG's picture and pixel size, a sound's duration with Play/Stop, or
// another file's kind and size - and what the build map says it became on
// the disc. A hint when `a` is null. Fills the current ImGui region (the
// caller's child window). `picture` is the PNG's texture, already loaded by
// the caller's cache; `sound_seconds` is the caller's cached result of
// rv_editor_asset_sound_seconds. `project` is the open project: when `a` is
// not covered by disc.toml and belongs in a disc section, an "Add to disc"
// button offers to put it there.
void rv_editor_asset_preview(const rv_editor_asset *a, const rv_editor_map_entry *entry, rv_editor_icon picture,
    double sound_seconds, const rv_editor_theme &theme, rv_editor_project &project);

// The asset's picture: a PNG's texture, loaded once and cached, or an empty icon otherwise.
rv_editor_icon rv_editor_asset_picture(SDL_Renderer *renderer, const rv_editor_asset &a);

} // namespace rv_editor
