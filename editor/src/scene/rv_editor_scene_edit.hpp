#pragma once

#include <array>
#include <string>
#include <vector>

#include "scene/rv_editor_scene.hpp"

namespace rv_editor
{

// A scene being edited: the document, its undo and redo, and the selection,
// which Hierarchy, the viewport and Inspector share by id.
struct rv_editor_scene_doc {
    rv_editor_scene scene;
    std::vector<std::vector<rv_editor_scene_object>> undo;
    std::vector<std::vector<rv_editor_scene_object>> redo;
    bool dirty = false;
    std::string selected;
};

// Before a change: the state as it is becomes one undo step. A drag calls it once, at its start.
void rv_editor_scene_step(rv_editor_scene_doc &doc);
// Back to the state the last step saved; false when there is none.
bool rv_editor_scene_undo(rv_editor_scene_doc &doc);
bool rv_editor_scene_redo(rv_editor_scene_doc &doc);

// Each change below is one undo step and returns what it made or why it did not.
std::string rv_editor_scene_add(rv_editor_scene_doc &doc, const std::string &kind, const std::string &parent);
// The object and everything under it.
void rv_editor_scene_delete(rv_editor_scene_doc &doc, const std::string &id);
// The object and everything under it, with new ids, beside the original.
std::string rv_editor_scene_duplicate(rv_editor_scene_doc &doc, const std::string &id);
// Under `parent` (empty: the root). keep_world keeps where it is in the scene and is
// refused when that needs a shear the file cannot hold; otherwise its local values stay.
// Returns RV_OK or RV_ERR_INVAL (bad parent, cycle, shear, or decompose failure).
int rv_editor_scene_reparent(rv_editor_scene_doc &doc,
    const std::string &id,
    const std::string &parent,
    bool keep_world,
    std::string &why);

// An object's transform in its parent's space and in the scene's, as a 3x4 matrix.
using rv_editor_affine = std::array<std::array<double, 4>, 3>;
rv_editor_affine rv_editor_scene_local(const rv_editor_scene_object &o);
rv_editor_affine rv_editor_scene_world(const rv_editor_scene &scene, int index);
rv_editor_affine rv_editor_affine_mul(const rv_editor_affine &a, const rv_editor_affine &b);
std::array<double, 3> rv_editor_affine_point(const rv_editor_affine &a, const std::array<double, 3> &p);
// A direction in `a`'s outer space brought into its inner one (`a` inverted, no translation).
std::array<double, 3> rv_editor_affine_solve(const rv_editor_affine &a, const std::array<double, 3> &dir);

// True when `ancestor` is `id` or above it.
bool rv_editor_scene_under(const rv_editor_scene &scene, const std::string &id, const std::string &ancestor);

} // namespace rv_editor
