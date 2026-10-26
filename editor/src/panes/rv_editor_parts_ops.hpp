#pragma once

#include <string>

#include "project/rv_editor_project.hpp"
#include "scene/rv_editor_scene_edit.hpp"

namespace rv_editor
{

// The picked pieces of the selected object's model, written as new .gltf files into the pending folder
// first; the scene changes only after the writes succeed. Each is one undo step.
// Delete parts: the picked pieces leave the model, which becomes the object's new model.
// Detach: the picked pieces become a new child object with their own model; the object keeps the rest.
// RV_OK, or RV_ERR_* with `error` set to one sentence naming the cause.
int rv_editor_parts_delete(const rv_editor_project &project, rv_editor_scene_doc &doc, std::string &error);
int rv_editor_parts_detach(const rv_editor_project &project, rv_editor_scene_doc &doc, std::string &error);

} // namespace rv_editor
