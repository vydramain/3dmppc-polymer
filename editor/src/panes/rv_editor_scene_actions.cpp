// The scene action bar's commands for the selected object or its picked pieces: each one's
// target, why it cannot run now, and its run. Hierarchy, the keys and the Scene toolbar call them.

#include "panes/rv_editor_scene_actions.hpp"

#include <array>
#include <string>
#include <vector>

#include "app/rv_editor_app.hpp"
#include "app/rv_editor_app_parts.hpp"
#include "log/rv_editor_log.hpp"
#include "panes/rv_editor_parts_ops.hpp"
#include "panes/rv_editor_scene_draw.hpp"
#include "pdk/rv_err.h"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// The table has one row per command, in the enum's order.
constexpr size_t action_count = static_cast<size_t>(rv_editor_scene_action::count);

// nullptr from why: the command can run. A run that fails sets `error` to the cause.
using why_fn = const char *(*)(const rv_editor_app &app);
using run_fn = int (*)(rv_editor_app &app, rv_editor_scene_camera &cam, std::string &error);

struct action_spec {
    rv_editor_scene_action action; // the row's command; checked against its index
    const char *label;             // text id
    const char *hint;              // text id
    why_fn why;
    run_fn run;
};

const char *object_block(const rv_editor_scene_doc &doc)
{
    if (doc.selected.empty()) {
        return rv_editor_text("scene_tools.nothing_selected");
    }
    if (rv_editor_scene_find(doc.scene, doc.selected) < 0) {
        return rv_editor_text("scene_edit.no_such_object");
    }
    return nullptr;
}

const char *scene_block(const rv_editor_app &app)
{
    if (app.scene == nullptr) {
        return rv_editor_text("scene_actions.no_scene");
    }
    return nullptr;
}

// A change also needs a scene that is not read-only.
const char *change_block(const rv_editor_app &app)
{
    if (const char *why = scene_block(app)) {
        return why;
    }
    if (!app.scene->scene.read_only.empty()) {
        return rv_editor_text("scene_actions.read_only");
    }
    return nullptr;
}

bool is_pieces(const rv_editor_scene_doc &doc)
{
    return rv_editor_scene_action_target_of(doc) == rv_editor_scene_action_target::pieces;
}

bool all_pieces(const rv_editor_scene_doc &doc)
{
    return doc.picked_pieces.size() == doc.picked_count;
}

const char *why_remove(const rv_editor_app &app)
{
    if (const char *why = change_block(app)) {
        return why;
    }
    const rv_editor_scene_doc &doc = *app.scene;
    if (is_pieces(doc)) {
        return all_pieces(doc) ? rv_editor_text("scene_actions.all_pieces_delete") : nullptr;
    }
    return object_block(doc);
}

const char *why_duplicate(const rv_editor_app &app)
{
    if (const char *why = change_block(app)) {
        return why;
    }
    const rv_editor_scene_doc &doc = *app.scene;
    if (is_pieces(doc)) {
        return rv_editor_text("scene_actions.duplicate_pieces");
    }
    return object_block(doc);
}

const char *why_detach(const rv_editor_app &app)
{
    if (const char *why = change_block(app)) {
        return why;
    }
    const rv_editor_scene_doc &doc = *app.scene;
    if (is_pieces(doc)) {
        return all_pieces(doc) ? rv_editor_text("scene_actions.all_pieces_detach") : nullptr;
    }
    if (const char *why = object_block(doc)) {
        return why;
    }
    const size_t at = static_cast<size_t>(rv_editor_scene_find(doc.scene, doc.selected));
    const rv_editor_scene_object &o = doc.scene.objects[at];
    return o.parent.empty() ? rv_editor_text("scene_actions.already_at_root") : nullptr;
}

const char *why_frame(const rv_editor_app &app)
{
    if (const char *why = scene_block(app)) {
        return why;
    }
    return object_block(*app.scene); // pieces are framed by their object
}

const char *why_hide(const rv_editor_app &app)
{
    if (const char *why = change_block(app)) {
        return why;
    }
    if (is_pieces(*app.scene)) {
        return rv_editor_text("scene_actions.pieces_hide");
    }
    return object_block(*app.scene);
}

const char *why_clear_picks(const rv_editor_app &app)
{
    if (const char *why = scene_block(app)) {
        return why;
    }
    return is_pieces(*app.scene) ? nullptr : rv_editor_text("scene_actions.no_pieces");
}

int missing_object(std::string &error)
{
    error = rv_editor_text("scene_edit.no_such_object");
    return RV_ERR_NOENT;
}

int run_remove(rv_editor_app &app, rv_editor_scene_camera &, std::string &error)
{
    rv_editor_scene_doc &doc = *app.scene;
    if (is_pieces(doc)) {
        return rv_editor_parts_delete(app.project, doc, error);
    }
    const std::string id = doc.selected;
    if (rv_editor_scene_find(doc.scene, id) < 0) {
        return missing_object(error);
    }
    rv_editor_scene_delete(doc, id);
    return RV_OK;
}

int run_duplicate(rv_editor_app &app, rv_editor_scene_camera &, std::string &error)
{
    rv_editor_scene_doc &doc = *app.scene;
    const std::string id = doc.selected;
    if (rv_editor_scene_duplicate(doc, id).empty()) {
        return missing_object(error);
    }
    return RV_OK;
}

int run_detach(rv_editor_app &app, rv_editor_scene_camera &, std::string &error)
{
    rv_editor_scene_doc &doc = *app.scene;
    if (is_pieces(doc)) {
        return rv_editor_parts_detach(app.project, doc, error);
    }
    const std::string id = doc.selected;
    return rv_editor_scene_reparent(doc, id, "", true, error);
}

int run_frame(rv_editor_app &app, rv_editor_scene_camera &cam, std::string &)
{
    rv_editor_scene_frame(app, cam, false);
    return RV_OK;
}

int run_hide(rv_editor_app &app, rv_editor_scene_camera &, std::string &error)
{
    rv_editor_scene_doc &doc = *app.scene;
    const int index = rv_editor_scene_find(doc.scene, doc.selected);
    if (index < 0) {
        return missing_object(error);
    }
    const size_t at = static_cast<size_t>(index);
    // As the Inspector's Visible checkbox: the object's undo step, then its hidden component flipped.
    const bool hidden = rv_editor_scene_has_component(doc.scene.objects[at], component_hidden);
    rv_editor_scene_step(doc);
    rv_editor_scene_object &o = doc.scene.objects[at];
    std::erase_if(o.components, [](const rv_editor_scene_component &c) {
        return c.name == component_hidden;
    });
    if (!hidden) {
        o.components.push_back(rv_editor_scene_component{ std::string(component_hidden), {} });
    }
    return RV_OK;
}

int run_clear_picks(rv_editor_app &app, rv_editor_scene_camera &, std::string &)
{
    rv_editor_scene_doc &doc = *app.scene;
    const std::string id = doc.selected;
    rv_editor_scene_select(doc, id);
    return RV_OK;
}

// Row i must be command i, with its texts and functions set: a missing or moved row fails the build.
constexpr bool rows_match(const std::array<action_spec, action_count> &table)
{
    for (size_t i = 0; i < table.size(); ++i) {
        const action_spec &row = table[i];
        if (row.action != static_cast<rv_editor_scene_action>(i) || row.label == nullptr || row.hint == nullptr ||
            row.why == nullptr || row.run == nullptr) {
            return false;
        }
    }
    return true;
}

constexpr std::array<action_spec, action_count> specs{ {
    { rv_editor_scene_action::remove, "scene_actions.delete", "scene_actions.delete_hint", why_remove, run_remove },
    { rv_editor_scene_action::duplicate,
        "scene_actions.duplicate",
        "scene_actions.duplicate_hint",
        why_duplicate,
        run_duplicate },
    { rv_editor_scene_action::detach, "scene_actions.detach", "scene_actions.detach_hint", why_detach, run_detach },
    { rv_editor_scene_action::frame, "scene_actions.frame", "scene_actions.frame_hint", why_frame, run_frame },
    { rv_editor_scene_action::hide, "scene_actions.hide", "scene_actions.hide_hint", why_hide, run_hide },
    { rv_editor_scene_action::clear_picks,
        "scene_actions.clear_picks",
        "scene_actions.clear_picks_hint",
        why_clear_picks,
        run_clear_picks },
} };
static_assert(rows_match(specs), "each scene action needs its row, in the enum's order");

const action_spec &spec_of(rv_editor_scene_action action)
{
    return specs[static_cast<size_t>(action)];
}

// A refused command: the cause goes to the log as a warning and to the Hierarchy note.
int refuse(rv_editor_app &app, const std::string &why, int code)
{
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, why);
    app.scene_move_note = why;
    return code;
}

} // namespace

rv_editor_scene_action_target rv_editor_scene_action_target_of(const rv_editor_scene_doc &doc)
{
    if (doc.selected.empty()) {
        return rv_editor_scene_action_target::none;
    }
    if (doc.picked_owner == doc.selected && !doc.picked_pieces.empty()) {
        return rv_editor_scene_action_target::pieces;
    }
    return rv_editor_scene_action_target::object;
}

const char *rv_editor_scene_action_why_not(const rv_editor_app &app, rv_editor_scene_action action)
{
    return spec_of(action).why(app);
}

const char *rv_editor_scene_action_label(rv_editor_scene_action action)
{
    return rv_editor_text(spec_of(action).label);
}

const char *rv_editor_scene_action_hint(rv_editor_scene_action action)
{
    return rv_editor_text(spec_of(action).hint);
}

int rv_editor_scene_action_run(rv_editor_app &app, rv_editor_scene_camera &cam, rv_editor_scene_action action)
{
    const action_spec &spec = spec_of(action);
    if (const char *why = spec.why(app)) {
        return refuse(app, why, RV_ERR_INVAL);
    }
    std::string error;
    const int code = spec.run(app, cam, error);
    if (code != RV_OK) {
        if (error.empty()) {
            error = rv_editor_text("scene_actions.failed");
        }
        return refuse(app, error, code);
    }
    app.scene_move_note.clear();
    return RV_OK;
}

} // namespace rv_editor
