// Runtime Controls and Project: views of the editor's models.

#include "panes/rv_editor_panes.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

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

void rv_editor_wrapped(const std::string &text, const rv_editor_theme &theme)
{
    rv_editor_label(text.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
}

// A group heading: the label in the bright text color, then a full-width separator.
void rv_editor_project_group(const char *label, const rv_editor_theme &theme)
{
    rv_editor_label(label, rv_editor_label_kind::bright, theme);
    rv_editor_separator();
}

// Millisecond extraction modulus for time formatting.
constexpr size_t ms_modulo = 1000;
// Time formatting buffer capacity.
constexpr size_t time_buffer_size = 32;

} // namespace

void rv_editor_dim(const std::string &text, const rv_editor_theme &theme)
{
    rv_editor_label(text.c_str(), rv_editor_label_kind::dim, theme, rv_editor_label_layout::wrap);
}

std::string rv_editor_clock(std::chrono::system_clock::time_point at)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(at);
    std::tm tm{};
    localtime_r(&t, &tm);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count() % ms_modulo;
    char buf[time_buffer_size];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

// One label and its value on a row of a two-column table.
void rv_editor_fact(const char *label, const std::string &value, const rv_editor_theme &theme)
{
    rv_editor_table_row();
    rv_editor_table_cell();
    rv_editor_label(label, rv_editor_label_kind::normal, theme);
    rv_editor_table_cell();
    rv_editor_label(value.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
}

void rv_editor_open_project_row(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_label(rv_editor_text("panes.no_project_open_message"), rv_editor_label_kind::normal, theme);
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("panes.open_project_button"), theme)) {
        app.open_folder_request = true;
    }
}

void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme)
{
    // Controls sit inside the content between its sections: the whole pane is one well.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const rv_editor_project &p = app.project;
    if (!p.open) {
        rv_editor_wrapped(rv_editor_text("panes.no_project_open_full"), theme);
    } else {
        rv_editor_project_group(rv_editor_text("panes.disc_heading"), theme);
        const std::string disc_id = p.disc_id.empty() ? rv_editor_text("panes.disc_id_unknown") : p.disc_id;
        const std::string disc_title = p.disc_title;
        std::string disc_info;
        if (p.disc_title.empty()) {
            disc_info = rv_editor_text_format("panes.disc_info", std::make_format_args(disc_id));
        } else {
            disc_info = rv_editor_text_format("panes.disc_info_with_title", std::make_format_args(disc_id, disc_title));
        }
        rv_editor_wrapped(disc_info, theme);
        if (!p.manifest_error.empty()) {
            rv_editor_wrapped(rv_editor_text("panes.manifest_parse_error") + p.manifest_error, theme);
        }
    }

    // The editor's own paths and tools, not disc.toml sections.
    rv_editor_project_group(rv_editor_text("panes.editor_heading"), theme);
    if (p.open) {
        rv_editor_path_row(rv_editor_text("panes.root_path"), p.root.string(), theme);
        rv_editor_path_row(rv_editor_text("panes.builds_path"), (p.cache_dir / builds_dir_name).string(), theme);
        rv_editor_path_row(rv_editor_text("panes.memory_card_path"), (p.state_dir / memcard_filename).string(), theme);
    }

    rv_editor_section(rv_editor_text("panes.toolchain_header"));
    for (size_t i = 0; i < std::size(tool_rows); ++i) {
        const rv_editor_tool &t = app.tools.*tool_rows[i].tool;
        rv_editor_status(rv_editor_text(tool_rows[i].name_id),
            t.problem.empty() ? rv_editor_status_kind::ok : rv_editor_status_kind::error,
            theme);
        ImGui::SameLine();
        if (!t.problem.empty()) {
            rv_editor_wrapped(t.problem, theme);
            continue;
        }
        const std::string origin = t.origin;
        const std::string origin_text = rv_editor_text_format("panes.tool_found_in", std::make_format_args(origin));
        const std::string version = t.version_check.running() ? rv_editor_text("panes.tool_checking_version") : t.version;
        rv_editor_wrapped(version.empty() ? origin_text : version + ", " + origin_text, theme);
        ImGui::PushID(i);
        rv_editor_path_row(rv_editor_text("panes.path_label"), t.path.string(), theme);
        ImGui::PopID();
    }
    rv_editor_state look_again;
    if (app.build.busy() || app.session.live()) {
        look_again = rv_editor_state{ rv_editor_look::live, rv_editor_text("panes.tools_not_while_running") };
    }
    if (rv_editor_button(rv_editor_text("panes.look_for_tools_button"), theme, look_again)) {
        rv_editor_app_init(app);
    }
    rv_editor_path_row(rv_editor_text("panes.settings_path"), app.tools.settings_path.string(), theme);
    if (!app.tools.settings_error.empty()) {
        rv_editor_wrapped(app.tools.settings_error, theme);
    }
    rv_editor_well_end();
}

} // namespace rv_editor
