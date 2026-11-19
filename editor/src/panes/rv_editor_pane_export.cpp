// Export: the open project as a disc, a player directory or a web page, written by
// mppcburner in the background. The burner's lines go to the log; this pane shows the state.

#include "panes/rv_editor_panes.hpp"

#include "panes/rv_editor_pane_export.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "imgui.h"

#include "build/rv_editor_export.hpp"
#include "build/rv_editor_export_setup.hpp"
#include "build/rv_editor_progress.hpp"
#include "panes/rv_editor_pane_say.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_field_sizes.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_steps.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

// The scheme that makes SDL_OpenURL open a local file in the desktop's browser.
constexpr const char *file_uri_prefix = "file://";

// Digits of a percent escape, and the bits one hex digit takes from a byte.
constexpr const char *uri_hex_digits = "0123456789ABCDEF";
constexpr unsigned hex_digit_bits = 4;
constexpr unsigned hex_digit_mask = 0x0F;

// The export form as edited until Export; one instance for the pane.
struct rv_editor_export_form {
    std::filesystem::path filled_for; // the project root the default directory was offered for
    rv_editor_export_kind kind = rv_editor_export_kind::disc;
    rv_editor_export_kind run_kind = rv_editor_export_kind::disc; // the kind of the last export that started
    bool has_run = false;                                         // an export started in this project
    char dest[filesystem_path_field_size] = {};
    rv_editor_export_paths paths;    // the destination of each kind, kept while the project stays open
    std::string error;               // why the last Export was refused, or why the default directory could not be offered
    rv_editor_path_probe dest_probe; // what the Directory field drew last
};

rv_editor_export_form export_form_draft;

// Puts the shown kind's remembered destination (by default <root>/export/<subdir>) into the field; a path too long for the
// field is refused, not cut.
void rv_editor_export_load_dest(rv_editor_export_form &form, const std::filesystem::path &root)
{
    const std::string dir = form.paths.get(root, form.kind);
    const int written = std::snprintf(form.dest, sizeof(form.dest), "%s", dir.c_str());
    if (written < 0 || static_cast<size_t>(written) >= sizeof(form.dest)) {
        form.dest[0] = '\0';
        form.error = rv_editor_text("pane_export.dest_too_long");
    }
}

// Offers the default directories once per project root.
void rv_editor_export_prefill(rv_editor_export_form &form, const std::filesystem::path &root)
{
    if (form.filled_for == root) {
        return;
    }
    form.filled_for = root;
    form.has_run = false; // the last export is another project's: its result is not shown
    form.error.clear();
    form.paths.reset();
    rv_editor_export_load_dest(form, root);
}

// The installed marker costs a filesystem hit, so it is re-read only when the pane reopens (a frame was skipped), the kind
// changes or a run ends (a setup may have just finished).
bool rv_editor_export_installed_cached(rv_editor_export_kind kind, bool busy)
{
    static bool installed = false;
    static bool was_busy = false;
    static int last_frame = -2;
    static rv_editor_export_kind last_kind = rv_editor_export_kind::disc;
    const int frame = ImGui::GetFrameCount();
    if (frame - last_frame > 1 || kind != last_kind || (was_busy && !busy)) {
        installed = rv_editor_web_support_installed(rv_editor_emscripten_dir());
    }
    last_frame = frame;
    last_kind = kind;
    was_busy = busy;
    return installed;
}

// A refusal's reason goes to the log under `key`.
void rv_editor_export_log_error(rv_editor_app &app, const char *key, const std::string &error)
{
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::error,
        rv_editor_text_format(key, std::make_format_args(error)));
}

int rv_editor_export_start(rv_editor_app &app, rv_editor_export_form &form)
{
    std::string error;
    const int err = app.exporter.start(app.project, app.tools, form.kind, std::filesystem::path(form.dest), app.log, error);
    if (err != RV_OK) {
        form.error = error;
        rv_editor_export_log_error(app, "pane_export.log_refused", error);
        return err;
    }
    form.run_kind = form.kind;
    form.has_run = true;
    form.error.clear();
    return RV_OK;
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

// Unreserved bytes of RFC 3986 and '/', which separates the path's segments; the rest is escaped.
bool rv_editor_export_uri_keeps(unsigned char byte)
{
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-' ||
        byte == '.' || byte == '_' || byte == '~' || byte == '/';
}

// The file:// URI of an absolute path (the caller checks it), every byte outside the kept set written as %XX.
std::string rv_editor_export_file_uri(const std::string &path)
{
    std::string uri = file_uri_prefix;
    for (const char ch : path) {
        const auto byte = static_cast<unsigned char>(ch);
        if (rv_editor_export_uri_keeps(byte)) {
            uri += ch;
            continue;
        }
        uri += '%';
        uri += uri_hex_digits[byte >> hex_digit_bits];
        uri += uri_hex_digits[byte & hex_digit_mask];
    }
    return uri;
}

// Hands the path to the desktop through SDL_OpenURL, which tries the OpenURI portal first.
int rv_editor_export_open_run(const std::filesystem::path &path, std::string &error)
{
    const std::string uri = rv_editor_export_file_uri(path.string());
    if (!SDL_OpenURL(uri.c_str())) {
        error = SDL_GetError();
        return RV_ERR_IO;
    }
    return RV_OK;
}

void rv_editor_export_open(rv_editor_app &app, rv_editor_export_form &form, const std::filesystem::path &path)
{
    std::string error;
    const int err = rv_editor_export_open_run(path, error);
    if (err != RV_OK) {
        form.error = error;
        rv_editor_export_log_error(app, "pane_export.log_open_refused", error);
        return;
    }
    form.error.clear();
}

// Installs the pinned web tools once per user.
void rv_editor_export_setup(rv_editor_app &app, rv_editor_export_form &form)
{
    std::string error;
    const int err = app.exporter.start_setup(app.tools, app.log, error);
    if (err != RV_OK) {
        form.error = error;
        rv_editor_export_log_error(app, "pane_export.log_setup_refused", error);
        return;
    }
    form.error.clear();
}

// The header's right side: the last export's kind, its run number and how long it ran, as m:ss.
std::string rv_editor_export_run_header(const rv_editor_export &exporter, const rv_editor_export_form &form)
{
    if (exporter.setup_job()) {
        return {};
    }
    // The kind that started, or the shown one when this project's form was filled after it started.
    const rv_editor_export_kind kind = form.has_run ? form.run_kind : form.kind;
    const std::string name = rv_editor_text(rv_editor_export_kinds()[static_cast<size_t>(kind)].name_key);
    const unsigned run = exporter.run();
    const auto minutes = static_cast<long long>(exporter.seconds() / 60);
    const auto seconds = static_cast<long long>(exporter.seconds() % 60);
    return rv_editor_text_format("pane_export.run_header", std::make_format_args(name, run, minutes, seconds));
}

// The result line's text for the job's state; empty while it runs (the strip says it) and before any export.
std::string rv_editor_export_result_line(const rv_editor_export_result_view &view, bool cancelled, bool shown)
{
    switch (view.kind) {
    case rv_editor_export_result_kind::succeeded:
        return rv_editor_text_format("pane_export.done", std::make_format_args(view.path));
    case rv_editor_export_result_kind::failed:
        return view.step.empty() ?
            rv_editor_text_format("pane_export.failed", std::make_format_args(view.reason)) :
            rv_editor_text_format("pane_export.failed_at", std::make_format_args(view.step, view.reason));
    case rv_editor_export_result_kind::running:
        return {};
    case rv_editor_export_result_kind::none:
        break;
    }
    return shown && cancelled ? rv_editor_text("pane_export.cancelled") : std::string();
}

// The label column of the form: the wider of its two labels, so the fields start at one edge.
float rv_editor_export_label_column()
{
    return std::max(ImGui::CalcTextSize(rv_editor_text("pane_export.block_kind")).x,
               ImGui::CalcTextSize(rv_editor_text("pane_export.dest_label")).x) +
        ImGui::GetStyle().ItemSpacing.x;
}

// The widest row: the box with its widest button row, or either shelf.
float rv_editor_export_content_width()
{
    return std::max({ rv_editor_message_box_min_width(
                          { { rv_editor_text("pane_export.setup_button") }, { rv_editor_text("panes.open_project_box") } }),
        rv_editor_say_shelf_width({ rv_editor_text("pane_export.export_button"), rv_editor_text("pane_export.cancel_button") }),
        rv_editor_say_shelf_width({ rv_editor_text("pane_export.open_folder_button"),
            rv_editor_text("pane_export.open_browser_button"),
            rv_editor_text("pane_export.show_problems_button") }) });
}

// The Kind row, its description in two lines under it and the Directory row.
void rv_editor_export_form_rows(rv_editor_app &app, rv_editor_export_form &form, float width, const rv_editor_theme &theme)
{
    const std::vector<std::string> names = rv_editor_export_kind_items();
    std::vector<const char *> items;
    for (const std::string &name : names) {
        items.push_back(name.c_str());
    }
    const float column = rv_editor_export_label_column();
    ImGui::AlignTextToFramePadding();
    rv_editor_label(rv_editor_text("pane_export.block_kind"),
        rv_editor_label_kind::normal,
        theme,
        rv_editor_label_layout::line);
    ImGui::SameLine(column);
    ImGui::SetNextItemWidth(-1.0f);
    int current = static_cast<int>(form.kind);
    const char *why =
        rv_editor_export_kind_change_allowed(app.exporter.busy()) ? nullptr : rv_editor_text("pane_export.busy_why");
    if (rv_editor_dropdown("##export_kind_combo",
            &current,
            items.data(),
            static_cast<int>(items.size()),
            theme,
            { { rv_editor_look::live, why } }) &&
        static_cast<size_t>(current) < names.size()) {
        rv_editor_pane_export_select_kind(rv_editor_export_kinds()[static_cast<size_t>(current)].kind);
    }
    rv_editor_message_text("##export_kind_desc",
        rv_editor_text(rv_editor_export_kinds()[static_cast<size_t>(form.kind)].description_key),
        rv_editor_label_kind::normal,
        theme,
        width);
    ImGui::AlignTextToFramePadding();
    rv_editor_label(rv_editor_text("pane_export.dest_label"),
        rv_editor_label_kind::normal,
        theme,
        rv_editor_label_layout::line);
    ImGui::SameLine(column);
    ImGui::SetNextItemWidth(-1.0f);
    if (rv_editor_path_field("##export_dest",
            form.dest,
            sizeof(form.dest),
            app.project.root.string(),
            theme,
            {},
            &form.dest_probe)) {
        form.paths.set(form.kind, form.dest);
    }
    rv_editor_item_tooltip(rv_editor_text("pane_export.replaces"));
}

// Export and Cancel on one shelf, the result's three buttons on the next: all there in every state, grey with the reason
// when they cannot act.
void rv_editor_export_shelves(rv_editor_app &app,
    rv_editor_export_form &form,
    bool installed,
    const rv_editor_export_result_view &view,
    const rv_editor_theme &theme)
{
    const rv_editor_export &exporter = app.exporter;
    rv_editor_shelf_begin("##export_shelf_run", theme);
    const char *start_why = app.project.open ? rv_editor_export_button_why(form.kind, installed, exporter.busy()) :
                                               rv_editor_text("pane_export.no_project_why");
    if (rv_editor_button(rv_editor_text("pane_export.export_button"), theme, { rv_editor_look::live, start_why }) &&
        app.project.open) {
        rv_editor_export_start(app, form);
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_export.cancel_button")));
    if (rv_editor_button(rv_editor_text("pane_export.cancel_button"),
            theme,
            { rv_editor_look::live, rv_editor_export_cancel_why(exporter) })) {
        rv_editor_export_cancel(app);
    }
    rv_editor_shelf_end();
    rv_editor_shelf_begin("##export_shelf_result", theme);
    const char *open_why = rv_editor_text("pane_export.open_why");
    if (rv_editor_button(rv_editor_text("pane_export.open_folder_button"),
            theme,
            { rv_editor_look::live, view.open_folder ? nullptr : open_why })) {
        std::error_code ec;
        const std::filesystem::path &result = exporter.result();
        rv_editor_export_open(app, form, std::filesystem::is_directory(result, ec) ? result : result.parent_path());
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_export.open_browser_button")));
    if (rv_editor_button(rv_editor_text("pane_export.open_browser_button"),
            theme,
            { rv_editor_look::live, view.open_browser ? nullptr : open_why })) {
        rv_editor_export_open(app, form, exporter.result());
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_export.show_problems_button")));
    if (rv_editor_button(rv_editor_text("pane_export.show_problems_button"),
            theme,
            { rv_editor_look::live, view.show_problems ? nullptr : rv_editor_text("pane_export.problems_why") })) {
        rv_editor_export_show_problems(app);
    }
    rv_editor_shelf_end();
}

// What the box says: a refusal or a failure of the form, that web support is missing (with its Install button), how the
// last export ended, or what Export does to the directory.
void rv_editor_export_say(rv_editor_app &app,
    rv_editor_export_form &form,
    bool installed,
    const rv_editor_export_result_view &view,
    bool shown,
    float width,
    const rv_editor_theme &theme)
{
    const rv_editor_export &exporter = app.exporter;
    std::string text;
    auto kind = rv_editor_label_kind::normal;
    std::vector<std::string> buttons;
    if (!form.error.empty()) {
        text = form.error;
        kind = rv_editor_label_kind::error;
    } else if (exporter.setup_job() && exporter.state() == rv_editor_build_state::failed) {
        text = exporter.failure_reason();
        kind = rv_editor_label_kind::error;
    } else if (exporter.busy()) {
        text.clear(); // the steps area says what runs
    } else if (rv_editor_export_kinds()[static_cast<size_t>(form.kind)].needs_web_tools && !installed) {
        text = rv_editor_text("pane_export.web_missing");
        buttons.push_back(rv_editor_text("pane_export.setup_button"));
    } else {
        text = rv_editor_export_result_line(view, exporter.state() == rv_editor_build_state::cancelled, shown);
        kind = view.kind == rv_editor_export_result_kind::failed ? rv_editor_label_kind::error :
            view.kind == rv_editor_export_result_kind::succeeded ? rv_editor_label_kind::info :
            text == rv_editor_text("pane_export.cancelled")      ? rv_editor_label_kind::warning :
                                                                   rv_editor_label_kind::normal;
    }
    const char *setup_hint = buttons.empty() ? nullptr : rv_editor_text("pane_export.setup_tooltip");
    if (rv_editor_message_box("##say", text, kind, buttons, theme, width, nullptr, nullptr, setup_hint) == 0) {
        rv_editor_export_setup(app, form);
    }
}

// The running job (an export or a web setup) in the content area: what it is, then its steps.
void rv_editor_export_steps(const rv_editor_export &exporter, const rv_editor_export_form &form, const rv_editor_theme &theme)
{
    if (!exporter.busy()) {
        return;
    }
    if (const std::string header = rv_editor_export_run_header(exporter, form); !header.empty()) {
        ImGui::TextUnformatted(header.c_str());
        rv_editor_say_whole_text();
    }
    if (exporter.progress().runs.empty()) {
        ImGui::TextUnformatted(rv_editor_text("pane_export.progress_starting"));
        rv_editor_say_whole_text();
    } else {
        rv_editor_steps_draw(exporter.progress().runs.back(), theme);
    }
}

} // namespace

rv_editor_size rv_editor_export_minimum()
{
    const float row = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    const float rows = 2.0f * row + rv_editor_message_text_height() + ImGui::GetStyle().ItemSpacing.y;
    return rv_editor_tiled_minimum(rv_editor_export_content_width(),
        rows + 2.0f * rv_editor_tiled_shelf_block() + rv_editor_tiled_box_block());
}

void rv_editor_pane_export(rv_editor_app &app, const rv_editor_theme &theme)
{
    const float width = rv_editor_export_content_width();
    rv_editor_export_form &form = export_form_draft;
    if (app.project.open) {
        rv_editor_export_prefill(form, app.project.root);
    }
    const rv_editor_export &exporter = app.exporter;
    const bool installed = rv_editor_export_installed_cached(form.kind, exporter.busy());
    const bool shown = form.has_run && !exporter.setup_job();
    const rv_editor_export_result_view view = shown ? rv_editor_export_result_make(exporter.state(),
                                                          exporter.result().string(),
                                                          exporter.failure_reason(),
                                                          exporter.progress(),
                                                          form.run_kind) :
                                                      rv_editor_export_result_view{};
    rv_editor_export_form_rows(app, form, width, theme);
    rv_editor_export_shelves(app, form, installed, view, theme);
    if (!rv_editor_say_no_project(app, theme, width)) {
        rv_editor_export_say(app, form, installed, view, shown, width, theme);
    }
    (void)rv_editor_tiled_area_begin("##export_steps", width, theme);
    rv_editor_export_steps(exporter, form, theme);
    rv_editor_tiled_area_end(theme);
}

std::vector<std::string> rv_editor_export_kind_items()
{
    std::vector<std::string> items;
    for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
        items.push_back(rv_editor_text(info.name_key));
    }
    return items;
}

const char *rv_editor_export_web_tools_disabled_text(rv_editor_export_kind kind)
{
    return rv_editor_export_kinds()[static_cast<size_t>(kind)].needs_web_tools ?
        nullptr :
        rv_editor_text("pane_export.web_tools_not_needed");
}

void rv_editor_pane_export_select_kind(rv_editor_export_kind kind)
{
    rv_editor_export_form &form = export_form_draft;
    form.paths.set(form.kind, form.dest);
    form.kind = kind;
    rv_editor_export_load_dest(form, form.filled_for);
}

int rv_editor_pane_export_start(rv_editor_app &app)
{
    return rv_editor_export_start(app, export_form_draft);
}

const rv_editor_path_probe &rv_editor_pane_export_dest_probe()
{
    return export_form_draft.dest_probe;
}

} // namespace rv_editor
