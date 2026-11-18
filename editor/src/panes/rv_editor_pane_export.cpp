// Export: the open project as a disc, a player directory or a web page, written by
// mppcburner in the background. The burner's lines go to the log; this pane shows the state.

#include "panes/rv_editor_panes.hpp"

#include "panes/rv_editor_pane_export.hpp"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>

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
    rv_editor_export_paths paths; // the destination of each kind, kept while the project stays open
    std::string error;            // why the last Export was refused, or why the default directory could not be offered
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

void rv_editor_export_start(rv_editor_app &app, rv_editor_export_form &form)
{
    std::string error;
    const int err = app.exporter.start(app.project, app.tools, form.kind, std::filesystem::path(form.dest), app.log, error);
    if (err != RV_OK) {
        form.error = error;
        rv_editor_export_log_error(app, "pane_export.log_refused", error);
        return;
    }
    form.run_kind = form.kind;
    form.has_run = true;
    form.error.clear();
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

// Layout of the Kind block: a card is at least this many font widths wide.
constexpr float card_min_em = 16.0f;

// A framed block with a header; `right` is dim text at the header's right edge. A block of height 0 follows its
// content. Always pair with rv_editor_export_block_end.
void rv_editor_export_block_begin(const char *id, const char *title, const std::string &right, const rv_editor_theme &theme)
{
    ImGui::BeginChild(id,
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar);
    rv_editor_label(title, rv_editor_label_kind::bright, theme);
    if (!right.empty()) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(right.c_str()).x + ImGui::GetCursorPosX());
        rv_editor_label(right.c_str(), rv_editor_label_kind::dim, theme);
    }
    ImGui::Separator();
}

void rv_editor_export_block_end()
{
    ImGui::EndChild();
}

// One card: radio with the name, the one-line description under it. True when the card was chosen.
bool rv_editor_export_card(const rv_editor_export_kind_info &info,
    bool active,
    const char *why,
    float width,
    float height,
    const rv_editor_theme &theme)
{
    const std::string id = "##export_card_" + std::string(info.subdir);
    ImGui::BeginChild(id.c_str(), ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    const bool chosen = rv_editor_radio(rv_editor_text(info.name_key), active, theme, { rv_editor_look::live, why });
    rv_editor_label(rv_editor_text(info.description_key), rv_editor_label_kind::dim, theme);
    ImGui::EndChild();
    return chosen;
}

// The Kind block: the cards in rows of as many as fit.
void rv_editor_export_kind_block(rv_editor_export_form &form,
    const std::filesystem::path &root,
    bool busy,
    const rv_editor_theme &theme)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const auto kinds = rv_editor_export_kinds();
    const int count = static_cast<int>(kinds.size());
    const float inner = ImGui::GetContentRegionAvail().x - 2.0f * style.WindowPadding.x;
    const int columns = rv_editor_export_card_columns(inner, ImGui::GetFontSize() * card_min_em, style.ItemSpacing.x, count);
    const float width = (inner - static_cast<float>(columns - 1) * style.ItemSpacing.x) / static_cast<float>(columns);
    const float height =
        ImGui::GetFrameHeight() + ImGui::GetTextLineHeight() + 2.0f * style.WindowPadding.y + style.ItemSpacing.y;

    rv_editor_export_block_begin("##export_block_kind", rv_editor_text("pane_export.block_kind"), {}, theme);
    const char *why = rv_editor_export_kind_change_allowed(busy) ? nullptr : rv_editor_text("pane_export.busy_why");
    for (int i = 0; i < count; ++i) {
        if (i % columns != 0) {
            ImGui::SameLine();
        }
        const rv_editor_export_kind_info &info = kinds[static_cast<size_t>(i)];
        if (rv_editor_export_card(info, form.kind == info.kind, why, width, height, theme)) {
            form.paths.set(form.kind, form.dest);
            form.kind = info.kind;
            rv_editor_export_load_dest(form, root);
        }
    }
    rv_editor_export_block_end();
}

void rv_editor_export_web_tools_block(rv_editor_app &app,
    rv_editor_export_form &form,
    bool installed,
    const rv_editor_theme &theme)
{
    const rv_editor_export &exporter = app.exporter;
    const bool busy = exporter.busy();
    rv_editor_export_block_begin("##export_block_web_tools", rv_editor_text("pane_export.block_web_tools"), {}, theme);
    if (busy && exporter.setup_job()) {
        if (exporter.progress().runs.empty()) {
            rv_editor_label(rv_editor_text("pane_export.progress_starting"), rv_editor_label_kind::normal, theme);
        } else {
            rv_editor_steps_draw(exporter.progress().runs.back(), theme);
        }
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
    rv_editor_label(rv_editor_text("pane_export.dest_label"), rv_editor_label_kind::normal, theme);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (rv_editor_text_field("##export_dest", form.dest, sizeof(form.dest), theme)) {
        form.paths.set(form.kind, form.dest);
    }
    rv_editor_item_tooltip(rv_editor_text("pane_export.dest_tooltip"));
    rv_editor_label(rv_editor_text("pane_export.replaces"), rv_editor_label_kind::dim, theme);
    const char *start_why = rv_editor_export_button_why(form.kind, installed, exporter.busy());
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
        rv_editor_label(form.error.c_str(), rv_editor_label_kind::error, theme, rv_editor_label_layout::wrap);
    }
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
    switch (view.kind) {
    case rv_editor_export_result_kind::running:
        if (exporter.progress().runs.empty()) {
            rv_editor_label(rv_editor_text("pane_export.progress_starting"), rv_editor_label_kind::normal, theme);
        } else {
            rv_editor_steps_draw(exporter.progress().runs.back(), theme);
        }
        break;
    case rv_editor_export_result_kind::succeeded: {
        const std::string line = rv_editor_text_format("pane_export.done", std::make_format_args(view.path));
        rv_editor_label(line.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
        if (view.open_browser && rv_editor_button(rv_editor_text("pane_export.open_browser_button"), theme)) {
            rv_editor_export_open(app, form, exporter.result());
        }
        if (view.open_folder) {
            ImGui::SameLine();
            if (rv_editor_button(rv_editor_text("pane_export.open_folder_button"), theme)) {
                std::error_code ec;
                const std::filesystem::path &result = exporter.result();
                rv_editor_export_open(app, form, std::filesystem::is_directory(result, ec) ? result : result.parent_path());
            }
        }
        break;
    }
    case rv_editor_export_result_kind::failed: {
        const std::string line = view.step.empty() ?
            rv_editor_text_format("pane_export.failed", std::make_format_args(view.reason)) :
            rv_editor_text_format("pane_export.failed_at", std::make_format_args(view.step, view.reason));
        rv_editor_label(line.c_str(), rv_editor_label_kind::error, theme, rv_editor_label_layout::wrap);
        if (view.show_problems && rv_editor_button(rv_editor_text("pane_export.show_problems_button"), theme)) {
            rv_editor_export_show_problems(app);
        }
        break;
    }
    default:
        rv_editor_label(rv_editor_text(shown && exporter.state() == rv_editor_build_state::cancelled ?
                                "pane_export.cancelled" :
                                "pane_export.nothing_yet"),
            rv_editor_label_kind::dim,
            theme);
        break;
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
    rv_editor_export_kind_block(form, app.project.root, busy, theme);
    if (rv_editor_export_blocks_shown(form.kind).web_tools) {
        rv_editor_export_web_tools_block(app, form, installed, theme);
    }
    rv_editor_export_output_block(app, form, installed, theme);
    rv_editor_export_progress_block(app, form, theme);
}

} // namespace rv_editor
