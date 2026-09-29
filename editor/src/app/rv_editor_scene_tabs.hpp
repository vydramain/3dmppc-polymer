#pragma once

#include <filesystem>
#include <vector>

namespace rv_editor
{

// A PNG or sound tab opened from Assets into the Scene tile, beside the scene itself.
enum class rv_editor_scene_tab_kind
{
    picture,
    sound,
};

struct rv_editor_scene_tab
{
    std::filesystem::path path;
    rv_editor_scene_tab_kind kind = rv_editor_scene_tab_kind::picture;
    double sound_seconds = 0.0; // read once at open, a sound's cached duration
};

// The Scene tile's tabs (editor/src/panes/rv_editor_pane_scene_tabs.cpp): the
// scene itself is tab 0, never stored here; `front` is 0 for the scene, else
// 1-based into `tabs`.
struct rv_editor_scene_tabs
{
    std::vector<rv_editor_scene_tab> tabs;
    int front = 0;
};

} // namespace rv_editor
