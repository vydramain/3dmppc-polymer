// Dispatch editor shortcuts before the panes read the frame's keyboard input.

#include "app/rv_editor_shell.hpp"

#include "imgui_internal.h"

namespace rv_editor
{

namespace
{

// Kind of the pane in the focused tile; empty when there is none.
rv_editor_pane_kind rv_editor_focused_pane_kind(const rv_editor_workspace &ws)
{
    if (ws.focused_leaf >= ws.layout.nodes.size()) {
        return rv_editor_pane_kind::empty;
    }
    const rv_editor_tile_node &node = ws.layout.nodes[ws.focused_leaf];
    if (node.kind != rv_editor_tile_kind::leaf || node.leaf.active >= node.leaf.tabs.size()) {
        return rv_editor_pane_kind::empty;
    }
    return ws.panes.panes[node.leaf.tabs[node.leaf.active]].kind;
}

} // namespace

bool rv_editor_shell_run_targets_candidate(const rv_editor_shell &shell)
{
    return shell.active == rv_editor_layout_preset::burn && !shell.app.session.live();
}

void rv_editor_shell_shortcuts(rv_editor_shell &shell)
{
    if (shell.settings_shortcut_capture) {
        return;
    }

    rv_editor_app &app = shell.app;
    const rv_editor_pane_kind focus = rv_editor_focused_pane_kind(shell.ws);
    const bool text_input = ImGui::GetIO().WantTextInput;
    const auto pressed = [&](rv_editor_shortcut_action action) {
        const auto &spec = rv_editor_shortcut_specs[static_cast<size_t>(action)];
        const ImGuiKeyChord chord = rv_editor_shell_shortcut_chord(shell, action);
        const bool remapped = chord != spec.default_chord;
        const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
        const bool function = key >= ImGuiKey_F1 && key <= ImGuiKey_F24;
        if (text_input && (spec.scope != rv_editor_shortcut_scope::global || (remapped && !function))) {
            return false;
        }
        if (spec.scope == rv_editor_shortcut_scope::scene && !rv_editor_scene_document_pane(focus)) {
            return false;
        }
        if (spec.scope == rv_editor_shortcut_scope::outside_code_and_terminal &&
            (focus == rv_editor_pane_kind::code || focus == rv_editor_pane_kind::terminal)) {
            return false;
        }
        if (!ImGui::IsKeyChordPressed(chord)) {
            return false;
        }
        // The shell runs before the panes. Keep this press and its repeats out of their input.
        ImGui::SetKeyOwner(key, ImGuiKeyOwner_Any, ImGuiInputFlags_LockUntilRelease);
        return true;
    };

    using action = rv_editor_shortcut_action;
    const bool burn = shell.active == rv_editor_layout_preset::burn;
    if (pressed(action::build)) {
        if (burn) {
            rv_editor_app_build_candidate(app);
        } else {
            rv_editor_app_build(app);
        }
    }
    if (rv_editor_shell_why_not_save(shell) == nullptr && pressed(action::save_scene)) {
        rv_editor_shell_scene_save(shell);
    }
    if (pressed(action::save_all)) {
        rv_editor_shell_save_all(shell);
    }
    if (pressed(action::open_project)) {
        rv_editor_shell_open_project(shell);
    }
    if (pressed(action::new_project)) {
        rv_editor_shell_page(shell, rv_editor_start_page::new_project);
    }
    if (pressed(action::find)) {
        app.project_search.focus = true;
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::search);
    }
    if (pressed(action::manual)) {
        rv_editor_shell_page(shell, rv_editor_start_page::manual);
    }
    if (pressed(action::run)) {
        if (rv_editor_shell_run_targets_candidate(shell)) {
            rv_editor_app_run_candidate(app);
        } else {
            rv_editor_app_run(app);
        }
    }
    if (pressed(action::focus_next)) {
        rv_editor_shell_focus_next(shell, false);
    }
    if (pressed(action::focus_previous)) {
        rv_editor_shell_focus_next(shell, true);
    }
    if (pressed(action::stop)) {
        rv_editor_app_stop(app);
    }
    if (pressed(action::pause)) {
        rv_editor_app_pause(app);
    }
    if (pressed(action::step)) {
        rv_editor_app_step(app);
    }
    if (pressed(action::reload)) {
        rv_editor_app_reload(app);
    }
}

} // namespace rv_editor
