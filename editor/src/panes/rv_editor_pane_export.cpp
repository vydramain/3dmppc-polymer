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
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_field_sizes.hpp"
#include "ui/rv_editor_widgets.hpp"
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

// Text lines the Web Tools body and the step strip keep for their longest variant; what needs more scrolls inside.
constexpr float web_body_lines = 2.0f;
constexpr float steps_lines = 3.0f;

// A framed block with a header; `right` is dim text at the header's right edge, cut with dots when it would reach the
// title. Always pair with rv_editor_export_block_end.
void rv_editor_export_block_begin(const char *id, const char *title, const std::string &right, const rv_editor_theme &theme)
{
    ImGui::BeginChild(id,
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar);
    rv_editor_label(title, rv_editor_label_kind::bright, theme, rv_editor_label_layout::line);
    const float room = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title).x - ImGui::GetStyle().ItemSpacing.x;
    if (!right.empty() && room > 0.0f) {
        const char *end = right.c_str() + right.size();
        const char *fit = rv_editor_text_fit(right.c_str(), end, room);
        const std::string shown =
            fit == end ? right : std::string(right.c_str(), fit) + rv_editor_text("widgets_fields.path_ellipsis");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(shown.c_str()).x + ImGui::GetCursorPosX());
        rv_editor_label(shown.c_str(), rv_editor_label_kind::dim, theme, rv_editor_label_layout::line);
    }
    ImGui::Separator();
}

void rv_editor_export_block_end()
{
    ImGui::EndChild();
}

// The Kind block: the dropdown over the table, the chosen kind's description in an area as tall as the tallest one.
void rv_editor_export_kind_block(const rv_editor_export_form &form, bool busy, const rv_editor_theme &theme)
{
    const std::vector<std::string> names = rv_editor_export_kind_items();
    std::vector<const char *> items;
    for (const std::string &name : names) {
        items.push_back(name.c_str());
    }
    rv_editor_export_block_begin("##export_block_kind", rv_editor_text("pane_export.block_kind"), {}, theme);
    int current = static_cast<int>(form.kind);
    ImGui::SetNextItemWidth(-1.0f);
    const char *why = rv_editor_export_kind_change_allowed(busy) ? nullptr : rv_editor_text("pane_export.busy_why");
    if (rv_editor_dropdown("##export_kind_combo",
            &current,
            items.data(),
            static_cast<int>(items.size()),
            theme,
            { { rv_editor_look::live, why } }) &&
        static_cast<size_t>(current) < names.size()) {
        rv_editor_pane_export_select_kind(rv_editor_export_kinds()[static_cast<size_t>(current)].kind);
    }
    ImGui::BeginChild("##export_kind_desc",
        ImVec2(0.0f, rv_editor_export_description_height(ImGui::GetContentRegionAvail().x)),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    rv_editor_label(rv_editor_text(rv_editor_export_kinds()[static_cast<size_t>(form.kind)].description_key),
        rv_editor_label_kind::dim,
        theme,
        rv_editor_label_layout::wrap);
    ImGui::EndChild();
    rv_editor_export_block_end();
}

// The Web Tools block: a constant rect for every kind and state; the body scrolls when its text needs more.
void rv_editor_export_web_tools_block(rv_editor_app &app,
    rv_editor_export_form &form,
    bool installed,
    const rv_editor_theme &theme)
{
    const rv_editor_export &exporter = app.exporter;
    const bool busy = exporter.busy();
    const rv_editor_export_kind_info &row = rv_editor_export_kinds()[static_cast<size_t>(form.kind)];
    const ImGuiStyle &style = ImGui::GetStyle();
    const float body = web_body_lines * ImGui::GetTextLineHeight() + style.ItemSpacing.y;
    rv_editor_export_block_begin("##export_block_web_tools", rv_editor_text("pane_export.block_web_tools"), {}, theme);
    ImGui::BeginChild("##export_web_body", ImVec2(0.0f, body), ImGuiChildFlags_None);
    if (busy && exporter.setup_job()) {
        if (exporter.progress().runs.empty()) {
            rv_editor_label(rv_editor_text("pane_export.progress_starting"), rv_editor_label_kind::normal, theme);
        } else {
            rv_editor_steps_draw(exporter.progress().runs.back(), theme);
        }
    } else if (!row.needs_web_tools) {
        rv_editor_label(rv_editor_export_web_tools_disabled_text(form.kind),
            rv_editor_label_kind::dim,
            theme,
            rv_editor_label_layout::wrap);
    } else if (installed) {
        rv_editor_label(rv_editor_text("pane_export.web_installed"), rv_editor_label_kind::dim, theme);
    } else {
        rv_editor_label(rv_editor_text("pane_export.web_missing"),
            rv_editor_label_kind::normal,
            theme,
            rv_editor_label_layout::wrap);
        const char *why = busy ? rv_editor_text("pane_export.setup_busy_why") : nullptr;
        if (rv_editor_button(rv_editor_text("pane_export.setup_button"), theme, { rv_editor_look::live, why })) {
            rv_editor_export_setup(app, form);
        }
        if (why == nullptr) {
            rv_editor_item_tooltip(rv_editor_text("pane_export.setup_tooltip"));
        }
        if (exporter.setup_job() && exporter.state() == rv_editor_build_state::failed) {
            rv_editor_label(exporter.failure_reason().c_str(),
                rv_editor_label_kind::error,
                theme,
                rv_editor_label_layout::wrap);
        }
    }
    ImGui::EndChild();
    rv_editor_export_block_end();
}

void rv_editor_export_output_block(rv_editor_app &app,
    rv_editor_export_form &form,
    bool installed,
    const rv_editor_theme &theme)
{
    const rv_editor_export &exporter = app.exporter;
    rv_editor_export_block_begin("##export_block_output", rv_editor_text("pane_export.block_output"), {}, theme);
    ImGui::AlignTextToFramePadding();
    rv_editor_label(rv_editor_text("pane_export.dest_label"),
        rv_editor_label_kind::normal,
        theme,
        rv_editor_label_layout::line);
    ImGui::SameLine();
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
    rv_editor_label(rv_editor_text("pane_export.replaces"), rv_editor_label_kind::dim, theme);
    const char *start_why = rv_editor_export_button_why(form.kind, installed, exporter.busy());
    if (rv_editor_button(rv_editor_text("pane_export.export_button"), theme, { rv_editor_look::live, start_why })) {
        rv_editor_export_start(app, form);
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_export.cancel_button")));
    if (rv_editor_button(rv_editor_text("pane_export.cancel_button"),
            theme,
            { rv_editor_look::live, rv_editor_export_cancel_why(exporter) })) {
        rv_editor_export_cancel(app);
    }
    rv_editor_message_line("##export_error", form.error.c_str(), 2, 0.0f, theme);
    rv_editor_export_block_end();
}

// The header's right side: the last export's kind, its run number and how long it ran, as m:ss.
std::string rv_editor_export_run_header(const rv_editor_export &exporter, const rv_editor_export_form &form)
{
    if (!form.has_run || exporter.setup_job()) {
        return {};
    }
    const std::string name = rv_editor_text(rv_editor_export_kinds()[static_cast<size_t>(form.run_kind)].name_key);
    const unsigned run = exporter.run();
    const auto minutes = static_cast<long long>(exporter.seconds() / 60);
    const auto seconds = static_cast<long long>(exporter.seconds() % 60);
    return rv_editor_text_format("pane_export.run_header", std::make_format_args(name, run, minutes, seconds));
}

// The result line's text for the job's state; empty while it runs (the strip says it).
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
    return rv_editor_text(shown && cancelled ? "pane_export.cancelled" : "pane_export.nothing_yet");
}

// The Progress block: every part is always drawn in its place; the buttons are enabled as the result allows.
void rv_editor_export_progress_block(rv_editor_app &app, rv_editor_export_form &form, const rv_editor_theme &theme)
{
    const rv_editor_export &exporter = app.exporter;
    const bool shown = form.has_run && !exporter.setup_job();
    rv_editor_export_block_begin("##export_block_progress",
        rv_editor_text("pane_export.block_progress"),
        rv_editor_export_run_header(exporter, form),
        theme);
    const rv_editor_export_result_view view = shown ? rv_editor_export_result_make(exporter.state(),
                                                          exporter.result().string(),
                                                          exporter.failure_reason(),
                                                          exporter.progress(),
                                                          form.run_kind) :
                                                      rv_editor_export_result_view{};
    ImGui::BeginChild("##export_steps",
        ImVec2(0.0f, steps_lines * ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y),
        ImGuiChildFlags_None);
    if (view.kind == rv_editor_export_result_kind::running) {
        if (exporter.progress().runs.empty()) {
            rv_editor_label(rv_editor_text("pane_export.progress_starting"), rv_editor_label_kind::normal, theme);
        } else {
            rv_editor_steps_draw(exporter.progress().runs.back(), theme);
        }
    }
    ImGui::EndChild();
    const std::string line = rv_editor_export_result_line(view, exporter.state() == rv_editor_build_state::cancelled, shown);
    rv_editor_message_line("##export_result", line.c_str(), 1, 0.0f, theme);
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
    rv_editor_export_block_end();
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
    const bool busy = app.exporter.busy();
    const bool installed = rv_editor_export_installed_cached(form.kind, busy);
    rv_editor_export_kind_block(form, busy, theme);
    rv_editor_export_web_tools_block(app, form, installed, theme);
    rv_editor_export_output_block(app, form, installed, theme);
    rv_editor_export_progress_block(app, form, theme);
}

std::vector<std::string> rv_editor_export_kind_items()
{
    std::vector<std::string> items;
    for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
        items.push_back(rv_editor_text(info.name_key));
    }
    return items;
}

float rv_editor_export_description_wrapped_height(rv_editor_export_kind kind, float width)
{
    const char *text = rv_editor_text(rv_editor_export_kinds()[static_cast<size_t>(kind)].description_key);
    return ImGui::CalcTextSize(text, nullptr, false, width).y;
}

float rv_editor_export_description_height(float width)
{
    float tallest = 0.0f;
    for (const rv_editor_export_kind_info &info : rv_editor_export_kinds()) {
        tallest = std::max(tallest, rv_editor_export_description_wrapped_height(info.kind, width));
    }
    return tallest;
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
