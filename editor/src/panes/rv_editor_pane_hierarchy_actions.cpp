// Hierarchy's shelf and target line: Add opens the object kinds menu, the command buttons run
// rv_editor_scene_action, and the target line says what they act on.

#include "panes/rv_editor_panes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_scene_actions.hpp"
#include "scene/rv_editor_scene.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

// Kinds the Add menu offers, in menu order
constexpr std::string_view kinds[] = { kind_group, kind_camera, kind_mesh, kind_quad, kind_billboard, kind_volume };

// A command's button: its code, and the theme colour it is drawn in.
struct rv_editor_hierarchy_command_spec {
    rv_editor_scene_action action;
    const char *code;
    uint32_t rv_editor_theme::*color;
};

// The shelf's commands, in order. Clear Picks sits on the target line instead.
constexpr rv_editor_hierarchy_command_spec shelf_commands[] = {
    { rv_editor_scene_action::remove, rv_editor_glyph::delete_, &rv_editor_theme::code_red },
    { rv_editor_scene_action::duplicate, rv_editor_glyph::duplicate, &rv_editor_theme::code_magenta },
    { rv_editor_scene_action::detach, rv_editor_glyph::detach, &rv_editor_theme::code_yellow },
    { rv_editor_scene_action::frame, rv_editor_glyph::frame, &rv_editor_theme::code_blue },
    { rv_editor_scene_action::hide, rv_editor_glyph::hide, &rv_editor_theme::text_bright },
};
constexpr rv_editor_hierarchy_command_spec clear_picks_command = {
    rv_editor_scene_action::clear_picks,
    rv_editor_glyph::clear_picks,
    &rv_editor_theme::code_cyan,
};

// Adds a child of kind under the selected group, else at the root.
void rv_editor_hierarchy_add(rv_editor_scene_doc &doc, const std::string &kind)
{
    const int sel = rv_editor_scene_find(doc.scene, doc.selected);
    const bool group = sel >= 0 && rv_editor_object_get_kind(doc.scene.objects[static_cast<size_t>(sel)]) == kind_group;
    rv_editor_scene_add(doc, kind, group ? doc.selected : std::string());
}

// A command button: its code in its colour, the hint as tooltip, and the reason when it cannot run.
void rv_editor_hierarchy_command(rv_editor_app &app,
    const rv_editor_ui_port &ui,
    const rv_editor_theme &theme,
    const rv_editor_hierarchy_command_spec &spec)
{
    const rv_editor_state state{ rv_editor_look::live, rv_editor_scene_action_why_not(app, spec.action) };
    ImGui::PushID(static_cast<int>(spec.action));
    const bool clicked = rv_editor_letter_button("##command",
        spec.code,
        theme.*(spec.color),
        rv_editor_scene_action_hint(spec.action),
        theme,
        state);
    ImGui::PopID();
    if (clicked) {
        rv_editor_hierarchy_run(app, ui, spec.action);
    }
}

} // namespace

std::span<const std::string_view> rv_editor_hierarchy_kinds()
{
    return kinds;
}

// Runs a command on a copy of the Scene camera; a camera it changed (Frame) is queued.
void rv_editor_hierarchy_run(rv_editor_app &app, const rv_editor_ui_port &ui, rv_editor_scene_action action)
{
    rv_editor_scene_camera cam = ui.state.scene_camera;
    rv_editor_scene_action_run(app, cam, action);
    if (cam != ui.state.scene_camera) {
        ui.requests.push_back({ .kind = rv_editor_ui_request_kind::set_scene_camera, .camera = cam });
    }
}

// Mesh reads "box" in the editor.
const char *rv_editor_scene_kind_label(const std::string &kind)
{
    return kind == kind_mesh ? "box" : kind.c_str();
}

void rv_editor_hierarchy_shelf(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    rv_editor_scene_doc &doc = *app.scene;
    // A read-only scene refuses Add and every command, saying why.
    const char *read_only_why = doc.scene.read_only.empty() ? nullptr : rv_editor_text("scene_actions.read_only");
    const rv_editor_state add_state{ rv_editor_look::live, read_only_why };
    const float side = ImGui::GetFrameHeight();
    rv_editor_shelf_begin("##shelf", theme);
    if (rv_editor_letter_button("##add",
            rv_editor_glyph::add,
            theme.code_green,
            rv_editor_text("pane_hierarchy.add_hint"),
            theme,
            add_state)) {
        ImGui::OpenPopup("##add_kinds");
    }
    if (rv_editor_popup_begin("##add_kinds")) {
        for (std::string_view kind : kinds) {
            const std::string kind_name(kind);
            if (rv_editor_menu_item(rv_editor_scene_kind_label(kind_name), nullptr, read_only_why)) {
                rv_editor_hierarchy_add(doc, kind_name);
            }
        }
        rv_editor_popup_end();
    }
    for (const rv_editor_hierarchy_command_spec &spec : shelf_commands) {
        rv_editor_flow(side);
        rv_editor_hierarchy_command(app, ui, theme, spec);
    }
    rv_editor_shelf_end();
}

void rv_editor_hierarchy_target(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    const rv_editor_scene_doc &doc = *app.scene;
    const rv_editor_scene_action_target target = rv_editor_scene_action_target_of(doc);
    const int index = rv_editor_scene_find(doc.scene, doc.selected);
    if (target == rv_editor_scene_action_target::none || index < 0) {
        rv_editor_label(rv_editor_text("pane_hierarchy.target_none"), rv_editor_label_kind::dim, theme);
        return;
    }
    const rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(index)];
    // The getter returns by value; object_name owns the bytes name points into.
    const std::string object_name = rv_editor_object_get_name(o);
    const char *name = object_name.empty() ? rv_editor_text("pane_hierarchy.unnamed") : object_name.c_str();
    if (target == rv_editor_scene_action_target::pieces) {
        const size_t count = doc.picked_pieces.size();
        const std::string text = rv_editor_text_format("pane_hierarchy.target_pieces", std::make_format_args(name, count));
        rv_editor_label(text.c_str(), rv_editor_label_kind::normal, theme);
        ImGui::SameLine();
        rv_editor_hierarchy_command(app, ui, theme, clear_picks_command);
        return;
    }
    // rv_editor_scene_kind_label returns kind.c_str(), or a literal for mesh; object_kind must outlive kind_label.
    const std::string object_kind = rv_editor_object_get_kind(o);
    const char *kind_label = rv_editor_scene_kind_label(object_kind);
    const std::string text = rv_editor_text_format("pane_hierarchy.node_label", std::make_format_args(name, kind_label));
    rv_editor_label(text.c_str(), rv_editor_label_kind::normal, theme);
}

} // namespace rv_editor
