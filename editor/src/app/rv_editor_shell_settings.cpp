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

#include "pdk/rv_err.h"
#include "app/rv_editor_shell.hpp"
#include "project/rv_editor_settings.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_settings_row
{
    const char *key;
    const char *label_text_id;
    const char *automatic_text_id; // what an empty field means
    rv_editor_tool rv_editor_toolchain::*tool;
};

// Pipeline order: baker bakes textures, burner writes the disc image, console
// runs it with devtools, player is the candidate's final check without them.
constexpr rv_editor_settings_row rv_editor_settings_rows[] = {
    { "baker", "shell_settings.label_baker", "shell_settings.empty_auto_next", &rv_editor_toolchain::baker },
    { "burner", "shell_settings.label_burner", "shell_settings.empty_auto_next", &rv_editor_toolchain::burner },
    { "console", "shell_settings.label_console", "shell_settings.empty_auto_next", &rv_editor_toolchain::console },
    { "player", "shell_settings.label_player", "shell_settings.empty_no_player", &rv_editor_toolchain::player },
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
    std::string ready;
    if (tool.version.empty()) {
        ready = rv_editor_text("shell_settings.status_ready");
    } else {
        const std::string &version = tool.version;
        ready = rv_editor_text_format("shell_settings.status_ready_version", std::make_format_args(version));
    }
    rv_editor_status(ready.c_str(), rv_editor_status_kind::ok, theme);
}

void rv_editor_settings_block(rv_editor_shell &shell, size_t i, float label_w, const rv_editor_theme &theme)
{
    const rv_editor_settings_row &row = rv_editor_settings_rows[i];
    const rv_editor_tool &tool = shell.app.tools.*row.tool;
    ImGui::PushID(static_cast<int>(i));
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
    ImGui::TextUnformatted(rv_editor_text(row.label_text_id));
    ImGui::PopStyleColor();

    rv_editor_settings_label(rv_editor_text("shell_settings.override_path"), label_w);
    const float browse_btn_w = rv_editor_button_width(rv_editor_text("shell_settings.browse_button"));
    const float buttons = browse_btn_w + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 10.0f, ImGui::GetContentRegionAvail().x - buttons));
    if (rv_editor_text_field("##path", shell.settings_paths[i], sizeof(shell.settings_paths[i]), theme)) {
        shell.settings_checks[i].reset();
    }
    ImGui::SameLine();
    const bool browsing = shell.settings_browse == static_cast<int>(i);
    const char *browse_tooltip = browsing ? rv_editor_text("shell_settings.browser_open_tooltip") : nullptr;
    if (rv_editor_button(rv_editor_text("shell_settings.browse_button"), theme,
            { rv_editor_look::live, browse_tooltip })) {
        const char *label_text = rv_editor_text(row.label_text_id);
        const std::string label_str = label_text;
        std::string browser_title = rv_editor_text_format("shell_settings.select_executable_for",
            std::make_format_args(label_str));
        std::filesystem::path start_path = tool.path;
        if (shell.settings_paths[i][0] != '\0') {
            start_path = shell.settings_paths[i];
        }
        rv_editor_browser_start(shell.settings_browser, browser_title,
            rv_editor_text("shell_settings.select_executable_button"), rv_editor_browse_pick::executable, start_path);
        shell.settings_browse = static_cast<int>(i);
    }
    if (shell.settings_paths[i][0] == '\0') {
        ImGui::SetCursorPosX(label_w);
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_disabled));
        ImGui::TextUnformatted(rv_editor_text(row.automatic_text_id));
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

    rv_editor_settings_label(rv_editor_text("shell_settings.resolved_path"), label_w);
    std::string resolved;
    if (tool.path.empty()) {
        resolved = rv_editor_text("shell_settings.path_none");
    } else {
        const std::string path_str = tool.path.string();
        const std::string &origin = tool.origin;
        resolved = rv_editor_text_format("shell_settings.resolved_with_origin",
            std::make_format_args(path_str, origin));
    }
    ImGui::TextWrapped("%s", resolved.c_str());

    rv_editor_settings_label(rv_editor_text("shell_settings.status_label"), label_w);
    const std::optional<rv_editor_tool> &checked = shell.settings_checks[i];
    rv_editor_settings_status(checked ? *checked : tool, theme);
    if (checked) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_disabled));
        ImGui::TextUnformatted(rv_editor_text("shell_settings.not_yet_applied"));
        ImGui::PopStyleColor();
    }
    ImGui::SetCursorPosX(label_w);
    if (rv_editor_button(rv_editor_text("shell_settings.check_button"), theme)) {
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
    rv_editor_pane_header(rv_editor_text("shell_settings.pane_header"), true, theme);
    const bool nowhere = app.tools.settings_path.empty();
    const char *file_info =
        nowhere ? rv_editor_text("shell_settings.no_file_desc") : app.tools.settings_path.c_str();
    auto msg = rv_editor_text_format("shell_settings.settings_desc", std::make_format_args(file_info));
    ImGui::TextWrapped("%s", msg.c_str());
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
    const char *no_dir_tooltip = nowhere ? rv_editor_text("shell_settings.no_dir_tooltip") : nullptr;
    const bool apply = rv_editor_button(rv_editor_text("shell_settings.apply_button"), theme,
        { rv_editor_look::live, no_dir_tooltip });
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("shell_settings.revert_button"), theme)) {
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
    if (rv_editor_settings_save_tools(app.tools.settings_path, tools, error) != RV_OK) {
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
