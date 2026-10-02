// Settings as a page: beside the Toolchest without a project, a tab with one.
// Toolchain only for now. The fields are a draft until Apply rewrites settings.toml
// [tools] section and looks the tools up again; Revert takes the file's values back.

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <optional>
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
    const char *automatic; // what an empty field means
    rv_editor_tool rv_editor_toolchain::*tool;
};

// Pipeline order: baker bakes textures, burner writes the disc image, console
// runs it with devtools, player is the candidate's final check without them.
constexpr rv_editor_settings_row rv_editor_settings_rows[] = {
    { "baker", "Baker: mppcbaker", "Empty: found automatically next to the editor", &rv_editor_toolchain::baker },
    { "burner", "Burner: mppcburner", "Empty: found automatically next to the editor", &rv_editor_toolchain::burner },
    { "console", "Runtime: 3dmppc built with devtools", "Empty: found automatically next to the editor",
        &rv_editor_toolchain::console },
    { "player", "Player: 3dmppc built without devtools, for a candidate's final check",
        "Empty: no player; it has no default place", &rv_editor_toolchain::player },
};
static_assert(std::size(rv_editor_settings_rows) == std::extent_v<decltype(rv_editor_shell::settings_paths)>);

// The draft from what settings.toml says; an empty field means the automatic place.
void rv_editor_settings_load(rv_editor_shell &shell)
{
    for (size_t i = 0; i < std::size(rv_editor_settings_rows); ++i) {
        const rv_editor_tool &tool = shell.app.tools.*rv_editor_settings_rows[i].tool;
        std::snprintf(shell.settings_paths[i], sizeof(shell.settings_paths[i]), "%s",
            tool.origin == "settings.toml" ? tool.path.c_str() : "");
        shell.settings_checks[i].reset();
    }
    shell.settings_error.clear();
    shell.settings_browse = -1;
    shell.settings_loaded = true;
}

// The label left of its value, or over it when label_w is 0 (a narrow tab).
void rv_editor_settings_label(const char *label, float label_w)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (label_w > 0.0f) {
        ImGui::SameLine(label_w);
    }
}

void rv_editor_settings_status(const rv_editor_tool &tool, const rv_editor_theme &theme)
{
    if (!tool.problem.empty()) {
        rv_editor_status(tool.problem.c_str(), rv_editor_status_kind::error, theme);
        return;
    }
    const std::string ready = tool.version.empty() ? "Ready" : "Ready: " + tool.version;
    rv_editor_status(ready.c_str(), rv_editor_status_kind::ok, theme);
}

void rv_editor_settings_block(rv_editor_shell &shell, size_t i, float label_w, const rv_editor_theme &theme)
{
    const rv_editor_settings_row &row = rv_editor_settings_rows[i];
    const rv_editor_tool &tool = shell.app.tools.*row.tool;
    ImGui::PushID(static_cast<int>(i));
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
    ImGui::TextUnformatted(row.label);
    ImGui::PopStyleColor();

    rv_editor_settings_label("Override path", label_w);
    const float buttons = rv_editor_button_width("Browse...") + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 10.0f, ImGui::GetContentRegionAvail().x - buttons));
    if (rv_editor_text_field("##path", shell.settings_paths[i], sizeof(shell.settings_paths[i]), theme)) {
        shell.settings_checks[i].reset();
    }
    ImGui::SameLine();
    const bool browsing = shell.settings_browse == static_cast<int>(i);
    if (rv_editor_button("Browse...", theme, { rv_editor_look::live, browsing ? "The browser is open below" : nullptr })) {
        rv_editor_browser_start(shell.settings_browser, std::string("Select executable for ") + row.label,
            "Select Executable", rv_editor_browse_pick::executable, shell.settings_paths[i][0] != '\0'
                ? std::filesystem::path(shell.settings_paths[i])
                : tool.path);
        shell.settings_browse = static_cast<int>(i);
    }
    if (shell.settings_paths[i][0] == '\0') {
        ImGui::SetCursorPosX(label_w);
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_disabled));
        ImGui::TextUnformatted(row.automatic);
        ImGui::PopStyleColor();
    }
    if (browsing) {
        std::filesystem::path picked;
        const rv_editor_browse_result r =
            rv_editor_browser_draw(shell.settings_browser, ImGui::GetFontSize() * 24.0f, nullptr, picked, theme);
        if (r == rv_editor_browse_result::picked) {
            std::snprintf(shell.settings_paths[i], sizeof(shell.settings_paths[i]), "%s", picked.c_str());
            shell.settings_checks[i].reset();
        }
        if (r != rv_editor_browse_result::none) {
            shell.settings_browse = -1;
        }
    }

    rv_editor_settings_label("Resolved path", label_w);
    const std::string resolved = tool.path.empty() ? "none" : tool.path.string() + " (" + tool.origin + ")";
    ImGui::TextWrapped("%s", resolved.c_str());

    rv_editor_settings_label("Status", label_w);
    const std::optional<rv_editor_tool> &checked = shell.settings_checks[i];
    rv_editor_settings_status(checked ? *checked : tool, theme);
    if (checked) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_disabled));
        ImGui::TextUnformatted("(the field as it stands, not yet applied)");
        ImGui::PopStyleColor();
    }
    ImGui::SetCursorPosX(label_w);
    if (rv_editor_button("Check", theme)) {
        shell.settings_checks[i] = rv_editor_tool_probe(row.key, shell.settings_paths[i]);
    }
    ImGui::PopID();
}

} // namespace

void rv_editor_page_settings(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    if (!shell.settings_loaded) {
        rv_editor_settings_load(shell);
    }
    rv_editor_pane_header("Settings: Toolchain", true, theme);
    const bool nowhere = app.tools.settings_path.empty();
    ImGui::TextWrapped("Kept in %s. Applying does not stop a build or a game: they keep the tools they started with.",
        nowhere ? "no file: neither XDG_CONFIG_HOME nor HOME is set" : app.tools.settings_path.c_str());
    ImGui::Separator();
    const float font = ImGui::GetFontSize();
    const float label_w = ImGui::GetContentRegionAvail().x >= font * 50.0f ? font * 15.0f : 0.0f;
    for (size_t i = 0; i < std::size(rv_editor_settings_rows); ++i) {
        rv_editor_settings_block(shell, i, label_w, theme);
        ImGui::Separator();
    }
    if (!shell.settings_error.empty()) {
        rv_editor_status(shell.settings_error.c_str(), rv_editor_status_kind::error, theme);
    }
    const bool apply = rv_editor_button("Apply", theme,
        { rv_editor_look::live, nowhere ? "No settings directory: set XDG_CONFIG_HOME or HOME" : nullptr });
    ImGui::SameLine();
    if (rv_editor_button("Revert", theme)) {
        shell.settings_loaded = false;
    }
    if (!apply) {
        return;
    }
    std::vector<rv_editor_settings_tool> tools;
    for (size_t i = 0; i < std::size(rv_editor_settings_rows); ++i) {
        tools.push_back({ rv_editor_settings_rows[i].key, shell.settings_paths[i] });
    }
    std::string error;
    if (!rv_editor_settings_save_tools(app.tools.settings_path, tools, error)) {
        shell.settings_error = error;
        return;
    }
    // Found again; a build or session already running keeps the tool it started with.
    app.tools = rv_editor_toolchain_find();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "settings saved: " + app.tools.settings_path.string());
    if (!app.tools.settings_error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, app.tools.settings_error);
    }
    shell.settings_loaded = false;
}

} // namespace rv_editor
