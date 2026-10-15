// Hierarchy's Pieces node under a mesh object: the pieces of its model, picked by click.
// The model is read only while the node is open, named and resolved as the viewport does it.

#include "panes/rv_editor_panes.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

#include "imgui.h"

#include "panes/rv_editor_scene_glb.hpp"
#include "project/rv_editor_project.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// The node's ImGui id; the object's row pushes its own id scope, so each object has its own node.
constexpr const char *rv_editor_pieces_id = "pieces";
// The piece row's id under its own PushID(number).
constexpr const char *rv_editor_piece_row_id = "piece";

// The .glb named `name` under [models], read (cached by mtime). RV_OK with `glb`, or the code with the reason in `error`.
int rv_editor_pieces_model(const rv_editor_project &project,
    const std::string &name,
    std::shared_ptr<const rv_editor_glb_model> &glb,
    std::string &error)
{
    if (!rv_editor_is_model_file_name(name)) {
        error = rv_editor_text_format("project.mesh_not_supported", std::make_format_args(name));
        return RV_ERR_INVAL;
    }
    std::filesystem::path path;
    if (const int rc = rv_editor_glb_resolve_name(project, name, path, error); rc != RV_OK) {
        return rc;
    }
    return rv_editor_glb_load(path, glb, error);
}

// A click picks the piece alone; Shift toggles it among the object's picks. Shift on another object's
// piece starts over: the object is selected first, which drops the old picks.
void rv_editor_hierarchy_pick_piece(rv_editor_scene_doc &doc,
    const rv_editor_scene_object &o,
    size_t number,
    size_t pieces,
    bool shift)
{
    const bool keep = shift && doc.picked_owner == o.id;
    if (!keep) {
        rv_editor_scene_select(doc, o.id);
    }
    doc.picked_owner = o.id;
    doc.picked_mesh = o.mesh;
    doc.picked_count = pieces;
    if (doc.picked_pieces.erase(number) == 0) {
        doc.picked_pieces.insert(number);
    }
}

// One piece by its number (from 1) and triangle count; highlighted while picked.
void rv_editor_hierarchy_piece_row(rv_editor_scene_doc &doc,
    const rv_editor_scene_object &o,
    size_t number,
    size_t tris,
    size_t pieces,
    const rv_editor_theme &theme)
{
    const bool picked = doc.picked_owner == o.id && doc.picked_pieces.count(number) != 0;
    const std::string label = rv_editor_text_format("pane_hierarchy.piece_row", std::make_format_args(number, tris));
    const rv_editor_list_style style = { .selected = picked };
    ImGui::PushID(static_cast<int>(number));
    if (rv_editor_list_row(rv_editor_piece_row_id, label.c_str(), theme, style).clicked) {
        rv_editor_hierarchy_pick_piece(doc, o, number, pieces, ImGui::GetIO().KeyShift);
    }
    ImGui::PopID();
}

// Every piece of the model, drawn only for the rows on screen.
void rv_editor_hierarchy_piece_rows(rv_editor_scene_doc &doc,
    const rv_editor_scene_object &o,
    const rv_editor_glb_model &glb,
    const rv_editor_theme &theme)
{
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(glb.pieces.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const size_t index = static_cast<size_t>(i);
            rv_editor_hierarchy_piece_row(doc, o, index + 1, glb.pieces[index].size(), glb.pieces.size(), theme);
        }
    }
}

} // namespace

void rv_editor_hierarchy_pieces(rv_editor_app &app, const rv_editor_scene_object &o, const rv_editor_theme &theme)
{
    if (o.kind != kind_mesh || o.mesh.empty()) {
        return;
    }
    rv_editor_scene_doc &doc = *app.scene;
    const std::string label = rv_editor_text("pane_hierarchy.pieces");
    const rv_editor_tree_result row = rv_editor_tree_row(rv_editor_pieces_id, label.c_str(), theme);
    if (!row.open) {
        return;
    }
    // Read only while open: the count is the first row, so the label needs no model.
    std::shared_ptr<const rv_editor_glb_model> glb;
    std::string error;
    if (const int rc = rv_editor_pieces_model(app.project, o.mesh, glb, error); rc != RV_OK) {
        rv_editor_dim(error);
        rv_editor_tree_pop();
        return;
    }
    const size_t count = glb->pieces.size();
    // The count is known only with the model, so picks from an older model are dropped here.
    rv_editor_scene_picks_check(doc, o, count);
    rv_editor_dim(rv_editor_text_format("pane_hierarchy.pieces_count", std::make_format_args(count)));
    rv_editor_hierarchy_piece_rows(doc, o, *glb, theme);
    rv_editor_tree_pop();
}

} // namespace rv_editor
