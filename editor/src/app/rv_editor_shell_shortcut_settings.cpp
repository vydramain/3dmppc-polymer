// Shortcut settings, validation and persistent chord names.

#include "app/rv_editor_shell_shortcut_settings.hpp"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

#include "imgui_internal.h"

#include "app/rv_editor_shell.hpp"
#include "project/rv_editor_settings.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

constexpr std::pair<ImGuiKeyChord, std::string_view> shortcut_modifiers[] = {
    { ImGuiMod_Ctrl, "Ctrl+" },
    { ImGuiMod_Shift, "Shift+" },
    { ImGuiMod_Alt, "Alt+" },
    { ImGuiMod_Super, "Super+" },
};

// These chords keep the pane actions documented in Help and the terminal's own controls.
constexpr std::pair<ImGuiKeyChord, const char *> pane_shortcuts[] = {
    { ImGuiMod_Ctrl | ImGuiKey_Z, "shell_menu.undo" },
    { ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, "shell_menu.redo" },
    { ImGuiMod_Ctrl | ImGuiKey_Y, "shell_menu.redo" },
    { ImGuiMod_Ctrl | ImGuiKey_D, "scene_actions.duplicate" },
    { ImGuiMod_Shift | ImGuiKey_Escape, "page_help.label_release_input" },
    { ImGuiKey_F2, "page_help.label_vim" },
    { ImGuiMod_Ctrl | ImGuiKey_C, "shell_settings.shortcut_terminal_interrupt" },
    { ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V, "pane_terminal.paste" },
};

constexpr rv_editor_shortcut_chords rv_editor_shortcut_defaults()
{
    rv_editor_shortcut_chords chords{};
    for (size_t i = 0; i < chords.size(); ++i) {
        chords[i] = rv_editor_shortcut_specs[i].default_chord;
    }
    return chords;
}

ImGuiKeyChord rv_editor_shortcut_parse(std::string_view name)
{
    ImGuiKeyChord modifiers = 0;
    for (const auto &[modifier, prefix] : shortcut_modifiers) {
        if (name.starts_with(prefix)) {
            modifiers |= modifier;
            name.remove_prefix(prefix.size());
        }
    }
    for (int i = ImGuiKey_Keyboard_BEGIN; i < ImGuiKey_Keyboard_END; ++i) {
        if (name == ImGui::GetKeyName(static_cast<ImGuiKey>(i))) {
            return modifiers | i;
        }
    }
    return ImGuiKey_None;
}

bool rv_editor_shortcut_valid(ImGuiKeyChord chord)
{
    const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    const bool function = key >= ImGuiKey_F1 && key <= ImGuiKey_F24;
    const bool modified = (chord & (ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Super)) != 0;
    return ImGui::IsKeyboardKey(key) && !ImGui::IsLRModKey(key) && (function || modified);
}

const char *rv_editor_shortcut_reserved(rv_editor_shortcut_action action, ImGuiKeyChord chord)
{
    if (action != rv_editor_shortcut_action::save_scene && chord == (ImGuiMod_Ctrl | ImGuiKey_S)) {
        return "shell_menu.save";
    }
    // ImGui handles window switching in NewFrame, before the shell can claim the chord.
    const ImGuiContext *const imgui = ImGui::GetCurrentContext();
    if (chord == (ImGuiMod_Ctrl | ImGuiKey_Tab) || chord == (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab) ||
        (imgui != nullptr && (chord == imgui->ConfigNavWindowingKeyNext || chord == imgui->ConfigNavWindowingKeyPrev))) {
        return "shell_settings.shortcut_window_navigation";
    }
    for (const auto &[reserved, label] : pane_shortcuts) {
        if (chord == reserved) {
            return label;
        }
    }
    return nullptr;
}

const rv_editor_shortcut_chords &rv_editor_shortcuts_active(rv_editor_shell &shell)
{
    if (!shell.shortcuts) {
        auto chords = rv_editor_shortcut_defaults();
        for (const auto &[key, name] : rv_editor_settings_read_shortcuts(shell.app.tools.settings_path)) {
            const auto *const spec = std::ranges::find_if(rv_editor_shortcut_specs, [&](const auto &s) {
                return key == s.key;
            });
            if (spec == rv_editor_shortcut_specs.end()) {
                continue;
            }
            const auto i = static_cast<size_t>(spec - rv_editor_shortcut_specs.begin());
            const auto action = static_cast<rv_editor_shortcut_action>(i);
            const ImGuiKeyChord chord = rv_editor_shortcut_parse(name);
            if (rv_editor_shortcut_valid(chord) && rv_editor_shortcut_reserved(action, chord) == nullptr) {
                chords[i] = chord;
            }
        }
        // Reject a conflicting hand edit as a set, keeping each command reachable on its default.
        for (auto *it = chords.begin(); it != chords.end(); ++it) {
            if (std::find(chords.begin(), it, *it) != it) {
                chords = rv_editor_shortcut_defaults();
                break;
            }
        }
        shell.shortcuts = chords;
    }
    return *shell.shortcuts;
}

} // namespace

std::string rv_editor_shortcut_name(ImGuiKeyChord chord)
{
    std::string name;
    for (const auto &[modifier, prefix] : shortcut_modifiers) {
        if ((chord & modifier) != 0) {
            name += prefix;
        }
    }
    name += ImGui::GetKeyName(static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_));
    return name;
}

void rv_editor_settings_shortcuts(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    if (rv_editor_button(rv_editor_text("shell_settings.shortcut_reset"), theme)) {
        rv_editor_shell_shortcuts_reset(shell);
    }
    const auto &chords = shell.settings_shortcuts ? *shell.settings_shortcuts : rv_editor_shortcuts_active(shell);
    const std::string popup = std::string(rv_editor_text("shell_settings.shortcut_change")) + "###shortcut_capture";
    constexpr rv_editor_table_column columns[] = {
        { "", rv_editor_table_width::fixed },
        { "", rv_editor_table_width::fixed },
    };
    constexpr rv_editor_table_style table_style{ .row_bands = true, .sizing = rv_editor_table_sizing::fixed_fit };
    if (rv_editor_table_begin("##shortcut_settings", columns, std::size(columns), table_style)) {
        for (size_t i = 0; i < chords.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            rv_editor_table_row();
            rv_editor_table_cell();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(rv_editor_text(rv_editor_shortcut_specs[i].label_id));
            rv_editor_table_cell();
            const std::string label = rv_editor_shortcut_name(chords[i]) + "###chord";
            if (rv_editor_button(label.c_str(), theme)) {
                shell.settings_shortcut_capture = static_cast<rv_editor_shortcut_action>(i);
                shell.settings_shortcut_error.clear();
            }
            ImGui::PopID();
        }
        rv_editor_table_end();
    }

    if (!shell.settings_shortcut_capture) {
        return;
    }
    if (!ImGui::IsPopupOpen(popup.c_str())) {
        ImGui::OpenPopup(popup.c_str());
    }
    if (!ImGui::BeginPopupModal(popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    rv_editor_label(rv_editor_text("shell_settings.shortcut_prompt"),
        rv_editor_label_kind::normal,
        theme,
        rv_editor_label_layout::line);
    if (!shell.settings_shortcut_error.empty()) {
        rv_editor_label(shell.settings_shortcut_error.c_str(),
            rv_editor_label_kind::error,
            theme,
            rv_editor_label_layout::line);
    }
    bool done = rv_editor_button(rv_editor_text("shell_settings.shortcut_cancel"), theme);
    if (!done && !ImGui::IsWindowAppearing()) {
        const ImGuiKeyChord mods = ImGui::GetIO().KeyMods;
        if (mods == 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            done = true;
        } else {
            for (int i = ImGuiKey_Keyboard_BEGIN; i < ImGuiKey_Keyboard_END; ++i) {
                const auto key = static_cast<ImGuiKey>(i);
                if (!ImGui::IsLRModKey(key) && ImGui::IsKeyPressed(key, false)) {
                    done = rv_editor_shell_shortcut_assign(shell,
                        *shell.settings_shortcut_capture,
                        mods | key,
                        shell.settings_shortcut_error);
                    break;
                }
            }
        }
    }
    if (done) {
        shell.settings_shortcut_capture.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

ImGuiKeyChord rv_editor_shell_shortcut_chord(rv_editor_shell &shell, rv_editor_shortcut_action action)
{
    return rv_editor_shortcuts_active(shell)[static_cast<size_t>(action)];
}

const char *rv_editor_shell_shortcut_label(rv_editor_shell &shell, rv_editor_shortcut_action action)
{
    return ImGui::GetKeyChordName(rv_editor_shell_shortcut_chord(shell, action));
}

bool rv_editor_shell_shortcut_assign(rv_editor_shell &shell,
    rv_editor_shortcut_action action,
    ImGuiKeyChord chord,
    std::string &error)
{
    if (!rv_editor_shortcut_valid(chord)) {
        error = rv_editor_text("shell_settings.shortcut_invalid");
        return false;
    }
    const auto &current = shell.settings_shortcuts ? *shell.settings_shortcuts : rv_editor_shortcuts_active(shell);
    const size_t assigned = static_cast<size_t>(action);
    const char *conflict = rv_editor_shortcut_reserved(action, chord);
    for (size_t i = 0; i < current.size(); ++i) {
        if (i != assigned && current[i] == chord) {
            conflict = rv_editor_shortcut_specs[i].label_id;
            break;
        }
    }
    if (conflict != nullptr) {
        const std::string name = rv_editor_shortcut_name(chord);
        const char *label = rv_editor_text(conflict);
        error = rv_editor_text_format("shell_settings.shortcut_conflict", std::make_format_args(name, label));
        return false;
    }
    auto draft = current;
    draft[assigned] = chord;
    shell.settings_shortcuts = draft;
    error.clear();
    return true;
}

void rv_editor_shell_shortcuts_reset(rv_editor_shell &shell)
{
    shell.settings_shortcuts = rv_editor_shortcut_defaults();
}

} // namespace rv_editor
