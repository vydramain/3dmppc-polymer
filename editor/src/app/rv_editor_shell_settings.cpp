// File > Settings: where the tools are (settings.toml [tools], ADR 0010). Save
// rewrites that section only and looks the tools up again.

#include <cstdio>
#include <iterator>
#include <type_traits>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "project/rv_editor_settings.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_settings_row
{
    const char *key;
    const char *label;
    rv_editor_tool rv_editor_toolchain::*tool;
};

constexpr rv_editor_settings_row rv_editor_settings_rows[] = {
    { "console", "Runtime: 3dmppc built with devtools", &rv_editor_toolchain::console },
    { "burner", "Burner: mppcburner", &rv_editor_toolchain::burner },
    { "baker", "Baker: mppcbaker", &rv_editor_toolchain::baker },
    { "player", "Player: 3dmppc built without devtools, for a candidate's final check", &rv_editor_toolchain::player },
};
static_assert(std::size(rv_editor_settings_rows) == std::extent_v<decltype(rv_editor_shell::settings_paths)>);

} // namespace

void rv_editor_shell_settings(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    const char *const title = "Settings";
    if (shell.settings_open) {
        shell.settings_open = false;
        // A field holds what settings.toml says; empty means the default place.
        for (size_t i = 0; i < std::size(rv_editor_settings_rows); ++i) {
            const rv_editor_tool &tool = app.tools.*rv_editor_settings_rows[i].tool;
            std::snprintf(shell.settings_paths[i], sizeof(shell.settings_paths[i]), "%s",
                tool.origin == "settings.toml" ? tool.path.c_str() : "");
        }
        shell.settings_error.clear();
        ImGui::OpenPopup(title);
    }
    if (!rv_editor_dialog_begin(title, theme)) {
        return;
    }
    const bool nowhere = app.tools.settings_path.empty();
    ImGui::TextWrapped("Tools, kept in %s. An empty field uses the one next to the editor; there is no default player.",
        nowhere ? "no file: neither XDG_CONFIG_HOME nor HOME is set" : app.tools.settings_path.c_str());
    for (size_t i = 0; i < std::size(rv_editor_settings_rows); ++i) {
        const rv_editor_tool &tool = app.tools.*rv_editor_settings_rows[i].tool;
        ImGui::PushID(static_cast<int>(i));
        ImGui::SeparatorText(rv_editor_settings_rows[i].label);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 36.0f);
        rv_editor_text_field("##path", shell.settings_paths[i], sizeof(shell.settings_paths[i]), theme);
        if (tool.problem.empty()) {
            ImGui::TextWrapped("In use: %s (%s)%s%s", tool.path.c_str(), tool.origin.c_str(),
                tool.version.empty() ? "" : ", ", tool.version.c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
            ImGui::TextWrapped("%s", tool.problem.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    if (!shell.settings_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s", shell.settings_error.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Separator();
    const bool save = rv_editor_button("Save", theme,
        { rv_editor_look::live, nowhere ? "No settings directory: set XDG_CONFIG_HOME or HOME" : nullptr });
    ImGui::SameLine();
    const bool cancel = rv_editor_button("Cancel", theme) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (save) {
        std::vector<rv_editor_settings_tool> tools;
        for (size_t i = 0; i < std::size(rv_editor_settings_rows); ++i) {
            tools.push_back({ rv_editor_settings_rows[i].key, shell.settings_paths[i] });
        }
        std::string error;
        if (rv_editor_settings_save_tools(app.tools.settings_path, tools, error)) {
            // Found again; a build or session already running keeps the tool it started with.
            app.tools = rv_editor_toolchain_find();
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
                "settings saved: " + app.tools.settings_path.string());
            if (!app.tools.settings_error.empty()) {
                app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, app.tools.settings_error);
            }
            ImGui::CloseCurrentPopup();
        } else {
            shell.settings_error = error;
        }
    }
    if (cancel) {
        ImGui::CloseCurrentPopup();
    }
    rv_editor_dialog_end();
}

} // namespace rv_editor
