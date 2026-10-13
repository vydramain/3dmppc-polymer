// Export: the open project as a disc, a player directory or a web page, written by
// mppcburner in the background. The burner's lines go to the log; this pane shows the state.

#include "panes/rv_editor_panes.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_field_sizes.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// The directory under the project root offered as the destination.
constexpr const char *export_dir_name = "export";

// Text keys of the kinds, in rv_editor_export_kind order.
constexpr std::array kind_keys = { "pane_export.kind_disc", "pane_export.kind_player", "pane_export.kind_web" };
static_assert(std::size(kind_keys) == static_cast<std::size_t>(rv_editor_export_kind::web) + 1);

// The export form as edited until Export; one instance for the pane.
struct rv_editor_export_form {
    std::filesystem::path filled_for; // the project root the default directory was offered for
    rv_editor_export_kind kind = rv_editor_export_kind::disc;
    char dest[filesystem_path_field_size] = {};
    std::string error; // why the last Export was refused, or why the default directory could not be offered
};

rv_editor_export_form export_form_draft;

rv_editor_status_kind rv_editor_export_lamp(rv_editor_build_state state)
{
    switch (state) {
    case rv_editor_build_state::succeeded:
        return rv_editor_status_kind::ok;
    case rv_editor_build_state::failed:
        return rv_editor_status_kind::error;
    case rv_editor_build_state::building:
    case rv_editor_build_state::cancelling:
        return rv_editor_status_kind::busy;
    case rv_editor_build_state::cancelled:
        return rv_editor_status_kind::warning;
    default:
        return rv_editor_status_kind::idle;
    }
}

// The job's state; once succeeded, the path the export wrote.
std::string rv_editor_export_state_line(const rv_editor_export &exporter)
{
    switch (exporter.state()) {
    case rv_editor_build_state::building:
        return rv_editor_text("pane_export.state_exporting");
    case rv_editor_build_state::cancelling:
        return rv_editor_text("pane_export.state_cancelling");
    case rv_editor_build_state::succeeded: {
        const std::string path = exporter.result().string();
        return rv_editor_text_format("pane_export.state_succeeded", std::make_format_args(path));
    }
    case rv_editor_build_state::failed:
        return rv_editor_text("pane_export.state_failed");
    case rv_editor_build_state::cancelled:
        return rv_editor_text("pane_export.state_cancelled");
    default:
        return rv_editor_text("pane_export.state_idle");
    }
}

void rv_editor_export_kinds(rv_editor_export_form &form, const rv_editor_theme &theme)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rv_editor_text("pane_export.kind_label"));
    for (size_t i = 0; i < kind_keys.size(); ++i) {
        const auto kind = static_cast<rv_editor_export_kind>(i);
        ImGui::SameLine();
        if (rv_editor_radio(rv_editor_text(kind_keys[i]), form.kind == kind, theme)) {
            form.kind = kind;
        }
    }
}

void rv_editor_export_dest(rv_editor_export_form &form, const rv_editor_theme &theme)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rv_editor_text("pane_export.dest_label"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##export_dest", form.dest, sizeof(form.dest), theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_export.dest_tooltip"));
}

// A refusal's reason goes to the log under `key`.
void rv_editor_export_log_error(rv_editor_app &app, const char *key, const std::string &error)
{
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::error,
        rv_editor_text_format(key, std::make_format_args(error)));
}

void rv_editor_export_start(rv_editor_app &app, rv_editor_export_form &form)
{
    std::string error;
    const int err = app.exporter.start(app.project, app.tools, form.kind, std::filesystem::path(form.dest), app.log, error);
    if (err != RV_OK) {
        form.error = error;
        rv_editor_export_log_error(app, "pane_export.log_refused", error);
        return;
    }
    form.error.clear();
}

// Offers <root>/export once per project root; a path too long for the field is refused, not cut.
void rv_editor_export_prefill(rv_editor_export_form &form, const std::filesystem::path &root)
{
    if (form.filled_for == root) {
        return;
    }
    form.filled_for = root;
    form.error.clear();
    const std::string dir = (root / export_dir_name).string();
    const int written = std::snprintf(form.dest, sizeof(form.dest), "%s", dir.c_str());
    if (written < 0 || static_cast<size_t>(written) >= sizeof(form.dest)) {
        form.dest[0] = '\0';
        form.error = rv_editor_text("pane_export.dest_too_long");
    }
}

// Cancel is live only while the export runs and is not already being cancelled.
const char *rv_editor_export_cancel_why(const rv_editor_export &exporter)
{
    if (exporter.state() == rv_editor_build_state::cancelling) {
        return rv_editor_text("pane_export.already_cancelling");
    }
    return exporter.busy() ? nullptr : rv_editor_text("pane_export.no_export_running");
}

void rv_editor_export_cancel(rv_editor_app &app)
{
    std::string error;
    if (app.exporter.cancel(error) != RV_OK) {
        rv_editor_export_log_error(app, "pane_export.log_cancel_refused", error);
    }
}

} // namespace

void rv_editor_pane_export(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    rv_editor_export_form &form = export_form_draft;
    rv_editor_export_prefill(form, app.project.root);
    rv_editor_export_kinds(form, theme);
    rv_editor_export_dest(form, theme);

    const rv_editor_export &exporter = app.exporter;
    const char *start_why = exporter.busy() ? rv_editor_text("pane_export.busy_why") : nullptr;
    if (rv_editor_button(rv_editor_text("pane_export.export_button"), theme, { rv_editor_look::live, start_why })) {
        rv_editor_export_start(app, form);
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_export.cancel_button"),
            theme,
            { rv_editor_look::live, rv_editor_export_cancel_why(exporter) })) {
        rv_editor_export_cancel(app);
    }
    if (!form.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s", form.error.c_str());
        ImGui::PopStyleColor();
    }
    const std::string line = rv_editor_export_state_line(exporter);
    rv_editor_status(line.c_str(), rv_editor_export_lamp(exporter.state()), theme);
}

} // namespace rv_editor
