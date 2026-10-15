#pragma once

#include <cstddef>

namespace rv_editor
{

struct rv_editor_app;
struct rv_editor_scene_camera;
struct rv_editor_scene_doc;

// The commands of the scene action bar: Hierarchy, the keys and the Scene toolbar run them alike.
enum class rv_editor_scene_action {
    remove,
    duplicate,
    detach,
    frame,
    hide,
    clear_picks,
    count // number of commands; the table has one row per command
};

// What a command acts on: nothing, the selected object, or the object's picked pieces.
enum class rv_editor_scene_action_target {
    none,
    object,
    pieces
};

rv_editor_scene_action_target rv_editor_scene_action_target_of(const rv_editor_scene_doc &doc);
// Why the command cannot run now, or nullptr when it can. The text lives until the next rv_editor_text_load (startup only).
const char *rv_editor_scene_action_why_not(const rv_editor_app &app, rv_editor_scene_action action);
// Button label and tooltip of a command, from the texts file (section scene_actions).
const char *rv_editor_scene_action_label(rv_editor_scene_action action);
const char *rv_editor_scene_action_hint(rv_editor_scene_action action);
// RV_OK, or RV_ERR_* with the cause in the Hierarchy note (scene_move_note) and the log. A command that
// why_not refuses returns RV_ERR_INVAL and changes nothing.
int rv_editor_scene_action_run(rv_editor_app &app, rv_editor_scene_camera &cam, rv_editor_scene_action action);

} // namespace rv_editor
