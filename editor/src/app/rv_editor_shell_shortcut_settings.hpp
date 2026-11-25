#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "imgui.h"

namespace rv_editor
{

struct rv_editor_shell;
struct rv_editor_theme;

enum class rv_editor_shortcut_action {
    new_project,
    open_project,
    save_scene,
    save_all,
    find,
    build,
    run,
    pause,
    step,
    stop,
    reload,
    focus_next,
    focus_previous,
    manual,
    count,
};

enum class rv_editor_shortcut_scope {
    global,
    outside_text,
    outside_code_and_terminal,
    scene,
};

struct rv_editor_shortcut_spec {
    const char *key;
    const char *group_id;
    const char *label_id;
    ImGuiKeyChord default_chord;
    rv_editor_shortcut_scope scope;
};

using rv_editor_shortcut_chords = std::array<ImGuiKeyChord, static_cast<std::size_t>(rv_editor_shortcut_action::count)>;

inline constexpr auto rv_editor_shortcut_specs = std::to_array<rv_editor_shortcut_spec>({
    { "new_project",
        "catalog_status.menu_file",
        "shell_start.new_project",
        ImGuiMod_Ctrl | ImGuiKey_N,
        rv_editor_shortcut_scope::outside_code_and_terminal },
    { "open_project",
        "catalog_status.menu_file",
        "shell_start.open_project",
        ImGuiMod_Ctrl | ImGuiKey_O,
        rv_editor_shortcut_scope::outside_code_and_terminal },
    { "save_scene",
        "catalog_status.menu_file",
        "shell_menu_scene.save_scene",
        ImGuiMod_Ctrl | ImGuiKey_S,
        rv_editor_shortcut_scope::scene },
    { "save_all",
        "catalog_status.menu_file",
        "catalog_status.menu_save_all",
        ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,
        rv_editor_shortcut_scope::outside_text },
    { "find",
        "shell_menu.edit",
        "shell_menu.find_in_project",
        ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F,
        rv_editor_shortcut_scope::global },
    { "build",
        "shell_menu.project",
        "widgets_status.button_build",
        ImGuiMod_Ctrl | ImGuiKey_B,
        rv_editor_shortcut_scope::outside_text },
    { "run", "catalog_status.menu_run", "page_help.label_run", ImGuiKey_F5, rv_editor_shortcut_scope::global },
    { "pause", "catalog_status.menu_run", "widgets_status.button_pause", ImGuiKey_F6, rv_editor_shortcut_scope::global },
    { "step", "catalog_status.menu_run", "widgets_status.button_step_frame", ImGuiKey_F7, rv_editor_shortcut_scope::global },
    { "stop",
        "catalog_status.menu_run",
        "widgets_status.button_stop",
        ImGuiMod_Shift | ImGuiKey_F5,
        rv_editor_shortcut_scope::global },
    { "reload", "catalog_status.menu_run", "widgets_status.tooltip_reload", ImGuiKey_F8, rv_editor_shortcut_scope::global },
    { "focus_next",
        "shell_menu.window",
        "shell_menu.focus_next_pane",
        ImGuiMod_Ctrl | ImGuiKey_F6,
        rv_editor_shortcut_scope::global },
    { "focus_previous",
        "shell_menu.window",
        "shell_menu.focus_prev_pane",
        ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F6,
        rv_editor_shortcut_scope::global },
    { "manual", "shell_menu.help", "manual.start_label", ImGuiKey_F1, rv_editor_shortcut_scope::global },
});
static_assert(rv_editor_shortcut_specs.size() == rv_editor_shortcut_chords{}.size());

// The spelling stored in settings.toml.
std::string rv_editor_shortcut_name(ImGuiKeyChord chord);
void rv_editor_settings_shortcuts(rv_editor_shell &shell, const rv_editor_theme &theme);

// The configured chord, or the shipped value when no override is saved.
ImGuiKeyChord rv_editor_shell_shortcut_chord(rv_editor_shell &shell, rv_editor_shortcut_action action);
const char *rv_editor_shell_shortcut_label(rv_editor_shell &shell, rv_editor_shortcut_action action);

// Stage a chord for Settings Apply. A conflict leaves the draft unchanged and supplies a reason.
bool rv_editor_shell_shortcut_assign(rv_editor_shell &shell,
    rv_editor_shortcut_action action,
    ImGuiKeyChord chord,
    std::string &error);

// Stage the original chords for Settings Apply.
void rv_editor_shell_shortcuts_reset(rv_editor_shell &shell);

} // namespace rv_editor
