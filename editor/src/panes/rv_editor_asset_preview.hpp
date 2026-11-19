#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "build/rv_editor_build_map.hpp"
#include "project/rv_editor_project.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_icons.hpp"

struct SDL_Renderer;

namespace rv_editor
{

// One resource file as Assets lists it.
struct rv_editor_asset {
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

// What a selected asset can be asked to do from the message box.
enum class rv_editor_asset_action {
    play,
    stop,
    add_to_disc
};

// What the preview says of the selected asset, for the message box: its path, what it is (a PNG's pixel size, a sound's
// duration, another file's kind and size), what the build map says it became on the disc and the last error; and the
// buttons that go with it (Play and Stop for a sound, Add to Disc when disc.toml does not cover it), each with its
// action. A hint when `a` is null. `picture` is the PNG's texture, already loaded by the caller's cache;
// `sound_seconds` is the caller's cached result of rv_editor_asset_sound_seconds.
struct rv_editor_asset_view {
    std::string text;
    std::vector<std::string> buttons;
    std::vector<rv_editor_asset_action> actions;
};
rv_editor_asset_view rv_editor_asset_preview(const rv_editor_asset *a,
    const rv_editor_map_entry *entry,
    rv_editor_icon picture,
    double sound_seconds,
    const rv_editor_project &project);

// Does what a button of the view asked.
void rv_editor_asset_act(rv_editor_asset_action action, const rv_editor_asset &a, rv_editor_project &project);

// The picture at the largest scale that fits `max_width` x `max_height`; nothing for an empty icon.
void rv_editor_asset_picture_show(rv_editor_icon picture, float max_width, float max_height);

// The asset's picture: a PNG's texture, loaded once and cached, or an empty icon otherwise.
rv_editor_icon rv_editor_asset_picture(SDL_Renderer *renderer, const rv_editor_asset &a);

// The picture's size at the largest whole-number scale that fits the box, or scaled down to fit when even 1x does not.
ImVec2 rv_editor_fit_picture(rv_editor_icon picture, float box_w, float box_h);

} // namespace rv_editor
