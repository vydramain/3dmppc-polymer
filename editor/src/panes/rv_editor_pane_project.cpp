// Project: a message box for what is wrong, the project's facts (S9) and the toolchain as records with a status.

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_pane_tiled.hpp"
#include "panes/rv_editor_panes.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_facts.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// Builds subdirectory in the project cache. Same value used in rv_editor_build.cpp, rv_editor_app_logs.cpp.
constexpr std::string_view builds_dir_name = "builds";

// Memory card file in the project's state directory. Same value used in rv_editor_app.cpp.
constexpr std::string_view memcard_filename = "memcard.mppccard";

// The tools the Toolchain list shows, in order: the member of rv_editor_toolchain and its name text.
struct rv_editor_tool_row {
    rv_editor_tool rv_editor_toolchain::*tool;
    const char *name_id;
};

constexpr rv_editor_tool_row tool_rows[] = {
    { &rv_editor_toolchain::console, "panes.tool_runtime" },
    { &rv_editor_toolchain::burner, "panes.tool_burner" },
    { &rv_editor_toolchain::firmware, "panes.tool_firmware" },
    { &rv_editor_toolchain::texbaker, "panes.tool_texbaker" },
    { &rv_editor_toolchain::musbaker, "panes.tool_musbaker" },
    { &rv_editor_toolchain::modbaker, "panes.tool_modbaker" },
    { &rv_editor_toolchain::inspector, "panes.tool_inspector" },
};
// Rows shown in the pane but not counted by the status bar: firmware.
constexpr size_t unlisted_tool_rows = 1;
static_assert(std::size(tool_rows) == std::size(rv_editor_toolchain_tools) + unlisted_tool_rows);

// Facts the first area lists.
constexpr int fact_rows = 6;

// The box's button rows over the states: Look for Tools Again, and Open Project beside it without a project.
std::vector<std::vector<std::string>> rv_editor_project_button_rows()
{
    return { { rv_editor_text("panes.look_for_tools_button") },
        { rv_editor_text("panes.look_for_tools_button"), rv_editor_text("panes.open_project_box") } };
}

float rv_editor_project_content_width()
{
    return rv_editor_message_strip_min_width(rv_editor_project_button_rows());
}

// What is wrong, one line each: the manifest, the tools that cannot run, the settings.
std::string rv_editor_project_trouble(const rv_editor_app &app)
{
    std::string out;
    const auto add = [&out](const std::string &line) {
        out += (out.empty() ? "" : "\n") + line;
    };
    if (!app.project.manifest_error.empty()) {
        add(rv_editor_text("panes.manifest_parse_error") + app.project.manifest_error);
    }
    for (const rv_editor_tool_row &row : tool_rows) {
        const rv_editor_tool &t = app.tools.*row.tool;
        if (!t.problem.empty()) {
            add(std::string(rv_editor_text(row.name_id)) + ": " + t.problem);
        }
    }
    if (!app.tools.settings_error.empty()) {
        add(app.tools.settings_error);
    }
    return out;
}

// What the strip under the records says: what is wrong (with Look for Tools Again), or that no project is open.
rv_editor_message_say rv_editor_project_say(const rv_editor_app &app)
{
    const std::string trouble = rv_editor_project_trouble(app);
    const bool open = app.project.open;
    if (trouble.empty() && open) {
        return {};
    }
    std::vector<std::string> buttons = { rv_editor_text("panes.look_for_tools_button") };
    if (!open) {
        buttons.emplace_back(rv_editor_text("panes.open_project_box"));
    }
    return { !trouble.empty() ? trouble : std::string(rv_editor_text("panes.no_project_open_message")),
        trouble.empty() ? rv_editor_label_kind::normal : rv_editor_label_kind::error,
        buttons };
}

void rv_editor_project_strip(rv_editor_app &app, const rv_editor_message_say &say, float width, const rv_editor_theme &theme)
{
    const bool running = app.build.busy() || app.session.live();
    const std::vector<rv_editor_state> states = { rv_editor_state{ rv_editor_look::live,
        running ? rv_editor_text("panes.tools_not_while_running") : nullptr } };
    const int pressed = rv_editor_message_strip("##strip", say.text, say.kind, say.buttons, theme, width, &states);
    if (pressed == 0 && !app.build.busy() && !app.session.live()) {
        rv_editor_app_init(app);
    } else if (pressed == 1) {
        app.open_folder_request = true;
    }
}

void rv_editor_project_facts(const rv_editor_app &app, float strip_h, const rv_editor_theme &theme)
{
    const rv_editor_project &p = app.project;
    const bool open = p.open;
    const auto path = [open](const std::filesystem::path &dir, std::string_view name) {
        return open ? (name.empty() ? dir : dir / name).string() : std::string();
    };
    // The facts list is as tall as its rows, but leaves the records their floor.
    const float rows = static_cast<float>(fact_rows);
    const float natural = rv_editor_tiled_rows_floor(rows, ImGui::GetTextLineHeightWithSpacing());
    const float room = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y - rv_editor_tiled_floor() -
        (strip_h > 0.0f ? strip_h + ImGui::GetStyle().ItemSpacing.y : 0.0f);
    (void)rv_editor_tiled_area_begin("##facts_area",
        rv_editor_project_content_width(),
        theme,
        { rv_editor_tiled_floor() },
        true,
        std::min(natural, room));
    const std::string disc = open ? (p.disc_id.empty() ? std::string(rv_editor_text("panes.disc_id_unknown")) : p.disc_id) : "";
    (void)rv_editor_facts_row(rv_editor_text("panes.disc_heading"), disc.c_str(), theme);
    (void)rv_editor_facts_row(rv_editor_text("panes.disc_title_key"), open ? p.disc_title.c_str() : "", theme);
    (void)rv_editor_facts_row(rv_editor_text("panes.root_path"), path(p.root, "").c_str(), theme);
    (void)rv_editor_facts_row(rv_editor_text("panes.builds_path"), path(p.cache_dir, builds_dir_name).c_str(), theme);
    (void)rv_editor_facts_row(rv_editor_text("panes.memory_card_path"), path(p.state_dir, memcard_filename).c_str(), theme);
    (void)rv_editor_facts_row(rv_editor_text("panes.settings_path"), app.tools.settings_path.string().c_str(), theme);
    rv_editor_tiled_area_end(theme);
}

void rv_editor_project_tools(const rv_editor_app &app, float strip_h, const rv_editor_theme &theme)
{
    (void)rv_editor_tiled_area_begin("##tools_area", rv_editor_project_content_width(), theme, {}, false, 0.0f, strip_h);
    const char *words[] = { rv_editor_text("panes.tool_status_ok"), rv_editor_text("panes.tool_status_problem") };
    rv_editor_record_style style;
    style.status_width = rv_editor_record_status_width(words, 2);
    for (size_t i = 0; i < std::size(tool_rows); ++i) {
        const rv_editor_tool &t = app.tools.*tool_rows[i].tool;
        std::string name = rv_editor_text(tool_rows[i].name_id);
        if (t.problem.empty()) {
            const std::string version = t.version_check.running() ? rv_editor_text("panes.tool_checking_version") : t.version;
            const std::string origin = t.origin;
            const std::string found = rv_editor_text_format("panes.tool_found_in", std::make_format_args(origin));
            name += "  " + (version.empty() ? found : version + ", " + found);
        }
        const std::string path = t.path.empty() ? "-" : t.path.string();
        const bool ok = t.problem.empty();
        ImGui::PushID(static_cast<int>(i));
        (void)rv_editor_record_row("##tool",
            name.c_str(),
            path.c_str(),
            words[ok ? 0 : 1],
            ok ? rv_editor_status_kind::ok : rv_editor_status_kind::error,
            theme,
            style);
        ImGui::PopID();
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace

rv_editor_size rv_editor_project_minimum()
{
    // The tallest strip, the facts list at its floor and the records.
    const float above = rv_editor_tiled_strip_block();
    return rv_editor_tiled_minimum(rv_editor_project_content_width(),
        above,
        { rv_editor_tiled_floor(), rv_editor_tiled_floor() });
}

void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_message_say say = rv_editor_project_say(app);
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, rv_editor_project_content_width());
    const float strip_h = rv_editor_message_strip_height(say.text, say.buttons, strip_w);
    rv_editor_project_facts(app, strip_h, theme);
    rv_editor_project_tools(app, strip_h, theme);
    rv_editor_project_strip(app, say, strip_w, theme);
}

} // namespace rv_editor
