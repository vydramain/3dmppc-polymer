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
#include "panes/rv_editor_pane_tiled.hpp"
#include "prefs/rv_editor_prefs.hpp"
#include "project/rv_editor_settings.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_form.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

// The override path fields and the frame rate slider are at least this wide, in font sizes.
constexpr float path_field_min_em = 14.0f;
constexpr float slider_min_em = 8.0f;

struct rv_editor_settings_row {
    const char *key;
    const char *label_text_id;
    const char *name_text_id;      // the short name: the form's label, the browser's title
    const char *automatic_text_id; // what an empty field means
    rv_editor_tool rv_editor_toolchain::*tool;
};

// Pipeline order: the bakers bake textures, sounds and models, burner writes the disc image (the bakers' directory holds
// mppcinspector too), console runs it with devtools, player is the candidate's final check without them.
constexpr rv_editor_settings_row rv_editor_settings_rows[] = {
    { "baker",
        "shell_settings.label_baker",
        "shell_settings.name_baker",
        "shell_settings.empty_auto_next",
        &rv_editor_toolchain::baker },
    { "burner",
        "shell_settings.label_burner",
        "shell_settings.name_burner",
        "shell_settings.empty_auto_next",
        &rv_editor_toolchain::burner },
    { "console",
        "shell_settings.label_console",
        "shell_settings.name_console",
        "shell_settings.empty_auto_next",
        &rv_editor_toolchain::console },
    { "player",
        "shell_settings.label_player",
        "shell_settings.name_player",
        "shell_settings.empty_no_player",
        &rv_editor_toolchain::player },
};
static_assert(std::size(rv_editor_settings_rows) == std::extent_v<decltype(rv_editor_shell::settings_paths)>);

// The draft from what settings.toml says; an empty field means the automatic place.
void rv_editor_settings_load(rv_editor_shell &shell)
{
    for (size_t i = 0; i < std::size(rv_editor_settings_rows); ++i) {
        const rv_editor_tool &tool = shell.app.tools.*rv_editor_settings_rows[i].tool;
        std::snprintf(shell.settings_paths[i],
            sizeof(shell.settings_paths[i]),
            "%s",
            tool.origin == "settings.toml" ? tool.path.c_str() : "");
        shell.settings_checks[i].reset();
    }
    shell.settings_error.clear();
    shell.settings_browse = -1;
    shell.settings_loaded = true;
}

// What each override path field drew last, for tests.
rv_editor_path_probe path_probes[std::extent_v<decltype(rv_editor_shell::settings_paths)>];

void rv_editor_settings_status(const rv_editor_tool &tool, const rv_editor_theme &theme)
{
    if (!tool.problem.empty()) {
        rv_editor_status(tool.problem.c_str(), rv_editor_status_kind::error, theme);
        return;
    }
    if (tool.version_check.running()) {
        rv_editor_status(rv_editor_text("shell_settings.status_checking_version"), rv_editor_status_kind::busy, theme);
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

// A Check's version check runs behind the frame; the page polls it until it ends.
void rv_editor_settings_poll_check(rv_editor_shell &shell, size_t i)
{
    std::optional<rv_editor_tool> &checked = shell.settings_checks[i];
    if (!checked || !checked->version_check.running()) {
        return;
    }
    std::string error;
    // A failure's reason lands in checked->version, which the status line shows.
    (void)rv_editor_tool_version_poll(*checked, error);
}

// The form's labels: the tools, then the frame pacing.
std::vector<rv_editor_form_row_desc> rv_editor_settings_form_rows()
{
    std::vector<rv_editor_form_row_desc> rows;
    for (const rv_editor_settings_row &row : rv_editor_settings_rows) {
        rows.push_back({ rv_editor_text(row.name_text_id), nullptr, nullptr, false });
    }
    rows.push_back({ rv_editor_text("shell_settings.pace_title"), nullptr, nullptr, false });
    return rows;
}

float rv_editor_settings_label_column()
{
    float width = 0.0f;
    for (const auto &row : rv_editor_settings_form_rows()) {
        width = std::max(width, ImGui::CalcTextSize(row.label).x);
    }
    return width + ImGui::GetStyle().ItemSpacing.x;
}

float rv_editor_settings_buttons_width()
{
    return rv_editor_button_width(rv_editor_text("shell_settings.apply_button")) +
        rv_editor_button_width(rv_editor_text("shell_settings.revert_button")) + ImGui::GetStyle().ItemSpacing.x;
}

// The widest row: a tool's path field at its floor with Browse and Check, the pacing row, the buttons, the box or the card.
float rv_editor_settings_content_width()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float label = rv_editor_settings_label_column();
    const float path = label + ImGui::GetFontSize() * path_field_min_em +
        rv_editor_button_width(rv_editor_text("shell_settings.browse_button")) +
        rv_editor_button_width(rv_editor_text("shell_settings.check_button")) + 2.0f * style.ItemSpacing.x;
    const char *fps = rv_editor_text("shell_settings.fps_limit_label");
    const float pace = label + rv_editor_checkbox_width(rv_editor_text("shell_settings.vsync_label")) + style.ItemSpacing.x +
        ImGui::GetFontSize() * slider_min_em + style.ItemSpacing.x + ImGui::CalcTextSize(fps).x;
    std::vector<std::string> purposes;
    for (const rv_editor_settings_row &row : rv_editor_settings_rows) {
        const std::string name = rv_editor_text(row.name_text_id);
        purposes.push_back(rv_editor_text_format("shell_settings.select_executable_for", std::make_format_args(name)));
    }
    const float card = rv_editor_browser_card_min_width(purposes, rv_editor_text("shell_settings.select_executable_button"));
    return std::max({ path, pace, rv_editor_settings_buttons_width(), card });
}

// The frame heights above the area and the row of buttons under it, each with its gap.
float rv_editor_settings_rows_height()
{
    const float rows = static_cast<float>(std::size(rv_editor_settings_rows) + 1);
    return (rows + 1.0f) * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
}

// One tool's lines in the content area: its name, where it was found, its status and what the field means.
void rv_editor_settings_lines(rv_editor_shell &shell, size_t i, const rv_editor_theme &theme)
{
    const rv_editor_settings_row &row = rv_editor_settings_rows[i];
    const rv_editor_tool &tool = shell.app.tools.*row.tool;
    ImGui::PushID(static_cast<int>(i));
    rv_editor_label(rv_editor_text(row.label_text_id), rv_editor_label_kind::bright, theme, rv_editor_label_layout::line);
    std::string resolved = rv_editor_text("shell_settings.path_none");
    if (!tool.path.empty()) {
        const std::string path_str = tool.path.string();
        const std::string &origin = tool.origin;
        resolved = rv_editor_text_format("shell_settings.resolved_with_origin", std::make_format_args(path_str, origin));
    }
    const std::string where = std::string(rv_editor_text("shell_settings.resolved_path")) + ": " + resolved;
    rv_editor_label(where.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
    const std::optional<rv_editor_tool> &checked = shell.settings_checks[i];
    rv_editor_settings_status(checked ? *checked : tool, theme);
    // What an empty field means, and that a Check ran on the field as it stands.
    std::string note;
    if (shell.settings_paths[i][0] == '\0') {
        note = rv_editor_text(row.automatic_text_id);
    }
    if (checked) {
        note += (note.empty() ? "" : " ") + std::string(rv_editor_text("shell_settings.not_yet_applied"));
    }
    if (!note.empty()) {
        rv_editor_label(note.c_str(), rv_editor_label_kind::dim, theme, rv_editor_label_layout::line);
    }
    ImGui::PopID();
}

// A tool's row: its name, the override path, Browse and Check.
void rv_editor_settings_row_draw(rv_editor_shell &shell, size_t i, const rv_editor_theme &theme)
{
    const rv_editor_settings_row &row = rv_editor_settings_rows[i];
    const rv_editor_tool &tool = shell.app.tools.*row.tool;
    rv_editor_settings_poll_check(shell, i);
    ImGui::PushID(static_cast<int>(i));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rv_editor_text(row.name_text_id));
    ImGui::SameLine(rv_editor_form_field_x());
    const float buttons = rv_editor_button_width(rv_editor_text("shell_settings.browse_button")) +
        rv_editor_button_width(rv_editor_text("shell_settings.check_button")) + 2.0f * ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - buttons, ImGui::GetFontSize()));
    if (rv_editor_path_field("##path",
            shell.settings_paths[i],
            sizeof(shell.settings_paths[i]),
            "",
            theme,
            {},
            &path_probes[i])) {
        shell.settings_checks[i].reset();
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("shell_settings.browse_button"), theme)) {
        const std::string name = rv_editor_text(row.name_text_id);
        std::filesystem::path start_path = tool.path;
        if (shell.settings_paths[i][0] != '\0') {
            start_path = shell.settings_paths[i];
        }
        rv_editor_browser_start(shell.settings_browser,
            rv_editor_text_format("shell_settings.select_executable_for", std::make_format_args(name)),
            rv_editor_text("shell_settings.select_executable_button"),
            rv_editor_browse_pick::executable,
            start_path);
        shell.settings_browse = static_cast<int>(i);
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("shell_settings.check_button"), theme)) {
        shell.settings_checks[i] = rv_editor_tool_probe(row.key, shell.settings_paths[i]);
    }
    ImGui::PopID();
}

// Frame pacing: vsync, or the frame rate limit while it is off. Applied at once, not by Apply;
// saved with the view settings at exit.
void rv_editor_settings_pace(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pace draft = shell.pace;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rv_editor_text("shell_settings.pace_title"));
    ImGui::SameLine(rv_editor_form_field_x());
    bool changed = rv_editor_checkbox(rv_editor_text("shell_settings.vsync_label"), &draft.vsync, theme);
    ImGui::SameLine();
    // The field follows what the window runs, not the request.
    ImGui::BeginDisabled(shell.vsync_active);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * slider_min_em);
    changed |= ImGui::SliderInt("##fps", &draft.fps_limit, fps_limit_min, fps_limit_max);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rv_editor_text("shell_settings.fps_limit_label"));
    ImGui::EndDisabled();
    if (changed) {
        shell.pace_request = draft;
    }
}

} // namespace

rv_editor_size rv_editor_settings_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_settings_content_width(), rv_editor_settings_rows_height());
}

void rv_editor_page_settings(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    if (!shell.settings_loaded) {
        rv_editor_settings_load(shell);
    }
    // Browsing is a mode of the page: the card takes the page's place and the form comes back after it.
    if (shell.settings_browse >= 0) {
        std::filesystem::path picked;
        const rv_editor_browse_result r = rv_editor_browser_card(shell.settings_browser, picked, theme);
        const size_t i = static_cast<size_t>(shell.settings_browse);
        if (r == rv_editor_browse_result::picked) {
            std::snprintf(shell.settings_paths[i], sizeof(shell.settings_paths[i]), "%s", picked.c_str());
            shell.settings_checks[i].reset();
        }
        if (r != rv_editor_browse_result::none) {
            shell.settings_browse = -1;
        }
        return;
    }
    const bool nowhere = app.tools.settings_path.empty();
    // Apply only asks; the frame after saves and looks up. Off while that or the version checks run.
    const bool searching =
        shell.settings_apply_request || app.tools.burner.version_check.running() || rv_editor_bakers_checking(app.tools);
    const bool refused = shell.pace.vsync && !shell.vsync_active;
    // What the page says besides a failed save: the search, a pacing note, else what the file is.
    const char *file_info = nowhere ? rv_editor_text("shell_settings.no_file_desc") : app.tools.settings_path.c_str();
    std::string said = rv_editor_text_format("shell_settings.settings_desc", std::make_format_args(file_info));
    if (refused) {
        said = rv_editor_text("shell_settings.vsync_refused_note");
    } else if (shell.vsync_active) {
        said = rv_editor_text("shell_settings.fps_limit_note");
    }
    if (searching) {
        said = rv_editor_text("shell_settings.searching_tools");
    }
    const std::vector<rv_editor_form_row_desc> rows = rv_editor_settings_form_rows();
    rv_editor_form_begin(rows, theme);
    for (size_t i = 0; i < std::size(rv_editor_settings_rows); ++i) {
        rv_editor_settings_row_draw(shell, i, theme);
    }
    rv_editor_settings_pace(shell, theme);
    rv_editor_form_end();

    (void)rv_editor_tiled_area_begin("##settings_area",
        rv_editor_settings_content_width(),
        theme,
        {},
        false,
        std::max(1.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.y));
    // The most pressing first: a failed save, then what the page says.
    if (!shell.settings_error.empty()) {
        rv_editor_label(shell.settings_error.c_str(), rv_editor_label_kind::error, theme, rv_editor_label_layout::wrap);
    }
    rv_editor_label(said.c_str(), rv_editor_label_kind::dim, theme, rv_editor_label_layout::wrap);
    for (size_t i = 0; i < std::size(rv_editor_settings_rows); ++i) {
        rv_editor_settings_lines(shell, i, theme);
    }
    rv_editor_tiled_area_end(theme);

    const char *no_dir_tooltip = nowhere ? rv_editor_text("shell_settings.no_dir_tooltip") : nullptr;
    const char *searching_text = searching ? rv_editor_text("shell_settings.searching_tools") : nullptr;
    const char *apply_block = searching_text != nullptr ? searching_text : no_dir_tooltip;
    if (rv_editor_button(rv_editor_text("shell_settings.apply_button"), theme, { rv_editor_look::live, apply_block })) {
        shell.settings_apply_request = true;
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("shell_settings.revert_button"), theme, { rv_editor_look::live, searching_text })) {
        shell.settings_loaded = false;
    }
}

void rv_editor_shell_settings_apply(rv_editor_shell &shell)
{
    if (!shell.settings_apply_request) {
        return;
    }
    shell.settings_apply_request = false;
    rv_editor_app &app = shell.app;
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

const rv_editor_path_probe &rv_editor_page_settings_path_probe(size_t i)
{
    return path_probes[i];
}

} // namespace rv_editor
