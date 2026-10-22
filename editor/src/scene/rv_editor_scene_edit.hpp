#pragma once

#include <array>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "scene/rv_editor_scene.hpp"

namespace rv_pdklib
{
struct rv_gte_matrix;
} // namespace rv_pdklib

namespace rv_editor
{

// How Hierarchy lists siblings: the file's order, or sorted by name, or by entity type then kind.
enum class rv_editor_hierarchy_sort {
    file_order,
    name,
    kind
};

// The Hierarchy search text's buffer size; an empty query shows the tree.
constexpr size_t hierarchy_query_size = 128;

// How an object is drawn: as is, as an outline, or not at all.
enum class rv_editor_visibility {
    shown,
    outline,
    absent
};

// The last refused edit of a field: its reason shows under the field until the document changes again.
struct rv_editor_scene_field_refusal {
    std::string object;
    std::string component;
    std::string key;
    std::string reason;
    uint64_t steps = 0; // doc.steps when refused: a new step changes it, so a stale reason hides
    size_t undo = 0;    // doc.undo.size() when refused: an undo or redo changes it, and so does a step
};

// A scene being edited: the document, its undo and redo, and the selection,
// which Hierarchy, the viewport and Inspector share by id. The Hierarchy search and
// sort, and the Scene's Hidden and layers, are view state, kept here but not written to the scene file.
struct rv_editor_scene_doc {
    rv_editor_scene scene;
    std::vector<std::vector<rv_editor_scene_object>> undo;
    std::vector<std::vector<rv_editor_scene_object>> redo;
    // Steps taken, never given back by undo: unlike undo.size(), it tells a new step from an undo and redo.
    uint64_t steps = 0;
    // The last refused field edit; it dies with the document.
    rv_editor_scene_field_refusal field_refusal;
    bool dirty = false;
    std::string selected;
    rv_editor_hierarchy_sort hierarchy_sort = rv_editor_hierarchy_sort::file_order;
    char hierarchy_query[hierarchy_query_size] = {};
    bool show_hidden = true;
    // A click picks a model's piece, not an object. View state.
    bool parts_mode = false;
    std::set<std::string, std::less<>> off_layers; // layers switched off by name; lookup by string_view
    // The pieces picked of the object picked_owner, numbered from 1 as Hierarchy shows them. View state, not undo.
    std::string picked_owner;
    std::string picked_mesh; // the model name of picked_owner when its pieces were picked
    size_t picked_count = 0; // the model's piece count then; a rewritten model drops the picks
    std::set<size_t> picked_pieces;
};

// Before a change: the state as it is becomes one undo step. A drag calls it once, at its start.
void rv_editor_scene_step(rv_editor_scene_doc &doc);
// Back to the state the last step saved; false when there is none.
bool rv_editor_scene_undo(rv_editor_scene_doc &doc);
bool rv_editor_scene_redo(rv_editor_scene_doc &doc);

// Selects `id` ("" for none); the pieces picked under the old selection are dropped. Not an undo step.
void rv_editor_scene_select(rv_editor_scene_doc &doc, const std::string &id);
// Drops the picks of `o` unless they were made on its current mesh with `pieces` pieces. Not an undo step.
void rv_editor_scene_picks_check(rv_editor_scene_doc &doc, const rv_editor_scene_object &o, size_t pieces);

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

// Shown or hidden by the hidden component marker: one undo step, none when already so.
// Precondition: `at` is the index of an existing object in doc.scene.objects.
void rv_editor_scene_set_visible(rv_editor_scene_doc &doc, size_t at, bool visible);

// An object's transform in its parent's space and in the scene's, as a 3x4 matrix.
using rv_editor_affine = std::array<std::array<double, 4>, 3>;
rv_editor_affine rv_editor_scene_local(const rv_editor_scene_object &o);
rv_editor_affine rv_editor_scene_world(const rv_editor_scene &scene, int index);
// Integer twins of the two above, in Q20.12 and 4.12. RV_OK, or RV_ERR_INVAL (range, a missing object or a parent cycle); out
// is then untouched and the caller shows the reason.
int rv_editor_scene_local_gte(const rv_editor_scene_object &o, rv_pdklib::rv_gte_matrix &out);
int rv_editor_scene_world_gte(const rv_editor_scene &scene, int index, rv_pdklib::rv_gte_matrix &out);
rv_editor_affine rv_editor_affine_mul(const rv_editor_affine &a, const rv_editor_affine &b);
std::array<double, 3> rv_editor_affine_point(const rv_editor_affine &a, const std::array<double, 3> &p);
// A direction in `a`'s outer space brought into its inner one (`a` inverted, no translation).
std::array<double, 3> rv_editor_affine_solve(const rv_editor_affine &a, const std::array<double, 3> &dir);

// True when `ancestor` is `id` or above it.
bool rv_editor_scene_under(const rv_editor_scene &scene, const std::string &id, const std::string &ancestor);

// The scene's layers: each object's components but hidden, unique, in order of first use.
// Layer names are ECS component names, shown as they are in the Layers menu, not interface text.
std::vector<std::string> rv_editor_scene_layers(const rv_editor_scene &scene);
// Visibility of `o`, an object of doc.scene: absent when a layer of it is off, or it is hidden with Hidden off;
// an outline when it is hidden with Hidden on. Allocates nothing: called per object per frame.
rv_editor_visibility rv_editor_scene_visibility(const rv_editor_scene_doc &doc, const rv_editor_scene_object &o);

} // namespace rv_editor
