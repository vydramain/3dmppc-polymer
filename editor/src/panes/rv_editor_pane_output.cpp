// Output, Runtime Log and Build Log: one component over the shared
// log, each view with its own sources, level, search, follow and wrap. Clear View
// hides what is there now; it deletes nothing.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Style colours pushed for the output well.
constexpr int pushed_style_colors = 3;

// Export format: level codes (Error, Warning, Info).
constexpr std::string_view export_level_error = "ERR";
constexpr std::string_view export_level_warning = "WRN";
constexpr std::string_view export_level_info = "INF";

// Export format: channel markers (stdout/stderr) and separators.
constexpr std::string_view export_channel_out = " out";
constexpr std::string_view export_channel_err = " err";
constexpr std::string_view export_channel_pid = " pid ";
constexpr std::string_view export_field_separator = " ";
constexpr std::string_view export_line_end = "\n";

// Log file storage: directory, prefix and extension.
constexpr std::string_view log_dir_name = "logs";
constexpr std::string_view log_file_prefix = "log-";
constexpr std::string_view log_file_ext = ".txt";

// Header: height padding and column table dimensions (4 columns, indexed 0-3).
constexpr float header_height_padding_px = 4.0f;
// Column alignment: offset by half a character width for row and border positioning.
constexpr float half_cell = 0.5f;
// Header text: centering by available height.
constexpr float center_factor = 0.5f;
constexpr size_t columns_count = 4;
constexpr size_t last_column_index = 3;
// Header border line: inset from top and bottom edges.
constexpr float header_border_inset_top_px = 2.0f;
constexpr float header_border_inset_bottom_px = 3.0f;
// Column resize handle: offset to center, and width.
constexpr float border_handle_left_offset_px = 3.0f;
constexpr float border_handle_width_px = 6.0f;
// Column sizing: minimum width in character cells.
constexpr float min_column_width_cells = 4.0f;

// Display: UI strings.
constexpr std::string_view sources_separator = ", ";
// Capitalized build source name, from rv_editor_log_source_name with capital=true.
constexpr std::string_view build_source_capitalized = "Build";
constexpr std::string_view build_number_separator = " #";
// Dropped lines flow: horizontal space multiplier in font sizes.
constexpr float dropped_lines_flow_width_em = 16.0f;
// Width measurement: sample character for monospace cell width.
constexpr std::string_view char_width_sample = "0";
// Message column: array index in view.columns[].
constexpr size_t message_column_index = 2;
// Text wrapping: minimum width in character cells.
constexpr float wrap_width_min_cells = 8.0f;

// UTF-8 byte classification: masks and markers.
constexpr unsigned char utf8_byte_type_mask = 0xc0;
constexpr unsigned char utf8_continuation_marker = 0x80;

// Log row highlight: semi-transparent alpha when hovered (ImGuiCol_HeaderHovered in Selectable rows).
constexpr unsigned header_hovered_alpha = 0x60u;

const char *rv_editor_output_level(rv_editor_log_level level)
{
    if (level == rv_editor_log_level::error) {
        return export_level_error.data();
    }
    if (level == rv_editor_log_level::warning) {
        return export_level_warning.data();
    }
    return export_level_info.data();
}

// Display text for log level in UI table.
const char *rv_editor_output_level_text(rv_editor_log_level level)
{
    if (level == rv_editor_log_level::error) {
        return rv_editor_text("pane_output.level_error");
    }
    if (level == rv_editor_log_level::warning) {
        return rv_editor_text("pane_output.level_warning");
    }
    return rv_editor_text("pane_output.level_info");
}

const char *rv_editor_output_kind(rv_editor_log_source source)
{
    switch (source) {
    case rv_editor_log_source::build:
        return rv_editor_text("pane_output.kind_build");
    case rv_editor_log_source::candidate:
        return rv_editor_text("pane_output.kind_candidate");
    default:
        return rv_editor_text("pane_output.kind_session"); // runtime and protocol
    }
}

// "pid N, <kind> #run, stdout|stderr", or "the editor" for the editor's own lines.
std::string rv_editor_output_origin(const rv_editor_log_line &line)
{
    if (line.pid == 0) {
        return rv_editor_text("pane_output.origin_editor");
    }
    const char *kind = rv_editor_output_kind(line.source);
    std::string out = rv_editor_text_format("pane_output.origin_pid",
        std::make_format_args(line.pid, kind, line.run));
    if (line.channel == rv_editor_log_channel::out) {
        out += rv_editor_text("pane_output.origin_stdout");
    } else if (line.channel == rv_editor_log_channel::err) {
        out += rv_editor_text("pane_output.origin_stderr");
    }
    return out;
}

std::string rv_editor_output_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The view's own choice of lines: sources, level, search.
bool rv_editor_output_passes(const rv_editor_output_view &view, const rv_editor_log_line &line, const std::string &needle)
{
    if (view.run_pid != 0) {
        if (line.pid != view.run_pid) {
            return false;
        }
    } else if (!view.show[static_cast<size_t>(line.source)]) {
        return false;
    }
    if (line.level < view.level) {
        return false;
    }
    return needle.empty() || rv_editor_output_lower(line.text).find(needle) != std::string::npos;
}

// The line as Copy and Export write it, pid and channel named after the source.
std::string rv_editor_output_text(const rv_editor_log_line &line)
{
    std::string channel;
    if (line.channel == rv_editor_log_channel::out) {
        channel = export_channel_out;
    } else if (line.channel == rv_editor_log_channel::err) {
        channel = export_channel_err;
    }
    const std::string stamp = rv_editor_log_stamp(line, true);
    const std::string level = rv_editor_output_level(line.level);
    const std::string source = rv_editor_log_source_name(line.source);
    const std::string head = stamp + export_field_separator.data() + level + export_field_separator.data() + source;
    const std::string pid_str = std::string(export_channel_pid) + std::to_string(line.pid);
    return head + pid_str + channel + export_field_separator.data() + line.text + export_line_end.data();
}

int rv_editor_output_export(rv_editor_app &app, const std::vector<const rv_editor_log_line *> &shown,
    std::string &where)
{
    const std::filesystem::path dir = app.project.cache_dir / log_dir_name.data();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string filename = std::string(log_file_prefix) + std::to_string(std::time(nullptr)) + log_file_ext.data();
    const std::filesystem::path path = dir / filename;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const rv_editor_log_line *line : shown) {
        out << rv_editor_output_text(*line);
    }
    out.close();
    if (!out) {
        const std::string path_str = path.string();
        where = rv_editor_text_format("pane_output.export_failed", std::make_format_args(path_str));
        return RV_ERR_IO;
    }
    where = path.string();
    return RV_OK;
}

// Time, Lvl, Source and Message over the lines, in the UI font but at the cells'
// code-font x positions below; the borders between them drag.
void rv_editor_output_header(rv_editor_output_view &view, float cell, const rv_editor_theme &theme)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight() + header_height_padding_px;
    const float w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), rv_editor_col(theme.code_base));
    dl->AddLine(ImVec2(p0.x, p0.y + h - 1.0f), ImVec2(p0.x + w, p0.y + h - 1.0f), rv_editor_col(theme.code_surface));
    dl->PushClipRect(p0, ImVec2(p0.x + w, p0.y + h), true);
    const char *names[] = { rv_editor_text("pane_output.col_time"), rv_editor_text("pane_output.col_level"),
        rv_editor_text("pane_output.col_source"), rv_editor_text("pane_output.col_message") };
    ImFont *ui_font = rv_editor_font_ui();
    const float ty = p0.y + (h - ui_font->LegacySize) * center_factor;
    float x = p0.x + cell * half_cell - view.scroll_x;
    for (size_t i = 0; i < columns_count; ++i) {
        dl->AddText(ui_font, ui_font->LegacySize, ImVec2(x, ty), rv_editor_col(theme.code_text), names[i]);
        if (i == last_column_index) {
            break;
        }
        x += view.columns[i] * cell;
        const float border = x - cell * half_cell;
        const ImVec2 line_start(border, p0.y + header_border_inset_top_px);
        const ImVec2 line_end(border, p0.y + h - header_border_inset_bottom_px);
        dl->AddLine(line_start, line_end, rv_editor_col(theme.code_surface));
        ImGui::SetCursorScreenPos(ImVec2(border - border_handle_left_offset_px, p0.y));
        ImGui::PushID(static_cast<int>(i));
        ImGui::InvisibleButton("##border", ImVec2(border_handle_width_px, h));
        ImGui::PopID();
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            view.columns[i] = std::max(min_column_width_cells, view.columns[i] + ImGui::GetIO().MouseDelta.x / cell);
        }
    }
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

} // namespace

// "All" (every source but the protocol trace), "All + protocol", or the names shown.
std::string rv_editor_output_sources(const rv_editor_output_view &view, bool capital)
{
    const bool all_but_protocol = view.show[static_cast<size_t>(rv_editor_log_source::editor)] &&
        view.show[static_cast<size_t>(rv_editor_log_source::build)] &&
        view.show[static_cast<size_t>(rv_editor_log_source::candidate)] &&
        view.show[static_cast<size_t>(rv_editor_log_source::runtime)];
    if (all_but_protocol) {
        const bool with_protocol = view.show[static_cast<size_t>(rv_editor_log_source::protocol)];
        const char *all_text = with_protocol ? "pane_output.sources_all_protocol" : "pane_output.sources_all";
        return rv_editor_text(all_text);
    }
    std::string out;
    for (size_t i = 0; i < view.show.size(); ++i) {
        if (!view.show[i]) {
            continue;
        }
        std::string name = rv_editor_log_source_name(static_cast<rv_editor_log_source>(i));
        if (capital) {
            name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        }
        out += (out.empty() ? "" : sources_separator.data()) + name;
    }
    return out.empty() ? rv_editor_text("pane_output.sources_none") : out;
}

std::string rv_editor_output_title(const rv_editor_app &app, rv_editor_pane_id pane)
{
    const auto it = app.outputs.find(pane);
    if (it == app.outputs.end()) {
        return rv_editor_text("pane_output.title_default");
    }
    const rv_editor_output_view &view = it->second;
    if (view.run_pid != 0) {
        return std::string(rv_editor_text("pane_output.title_prefix")) + view.run_label;
    }
    std::string what = rv_editor_output_sources(view, true);
    if (what == build_source_capitalized && app.build.number() != 0) {
        what += build_number_separator.data() + std::to_string(app.build.number());
    }
    if (view.level != rv_editor_log_level::info) {
        const bool errors_only = view.level == rv_editor_log_level::error;
        const char *level_text = errors_only ? "pane_output.title_errors" : "pane_output.title_warnings";
        what += rv_editor_text(level_text);
    }
    return std::string(rv_editor_text("pane_output.title_prefix")) + what;
}

void rv_editor_pane_output(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    rv_editor_output_view &view = app.outputs[pane];
    const rv_editor_log &log = app.log;

    // Controls, filters and notes draw in the UI font; only the line cells below
    // are in the code font.
    bool copy = false;
    bool exporting = false;
    rv_editor_shelf_begin("##shelf", theme);
    rv_editor_output_controls(app, view, copy, exporting, theme);
    rv_editor_shelf_end();

    rv_editor_well_begin("##well", ImVec2(0, 0), theme, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // The lines, and what the filters keep out of sight.
    const std::string needle = rv_editor_output_lower(view.search);
    std::vector<const rv_editor_log_line *> shown;
    size_t hidden_errors = 0;
    size_t hidden_warnings = 0;
    size_t run_lines = 0;
    for (const rv_editor_log_line &line : log.lines()) {
        run_lines += line.pid == view.run_pid ? 1 : 0;
        if (line.seq < view.hide_before) {
            continue;
        }
        if (rv_editor_output_passes(view, line, needle)) {
            shown.push_back(&line);
        } else if (line.source != rv_editor_log_source::protocol) {
            hidden_errors += line.level == rv_editor_log_level::error ? 1 : 0;
            hidden_warnings += line.level == rv_editor_log_level::warning ? 1 : 0;
        }
    }
    if (view.run_pid != 0 && run_lines == 0) {
        const std::string msg = rv_editor_text_format("pane_output.status_no_lines",
            std::make_format_args(view.run_label));
        rv_editor_status(msg.c_str(), rv_editor_status_kind::idle, theme);
    }
    if (hidden_errors + hidden_warnings != 0) {
        const std::string hidden = rv_editor_text_format("pane_output.status_hidden",
            std::make_format_args(hidden_errors, hidden_warnings));
        rv_editor_status(hidden.c_str(), hidden_errors != 0 ? rv_editor_status_kind::error : rv_editor_status_kind::warning,
            theme);
    }
    if (log.dropped() != 0) {
        rv_editor_flow(ImGui::GetFontSize() * dropped_lines_flow_width_em);
        const unsigned long long dropped = log.dropped();
        const std::string msg = rv_editor_text_format("pane_output.info_dropped",
            std::make_format_args(dropped));
        ImGui::TextUnformatted(msg.c_str());
    }
    const auto picked = [&view](const rv_editor_log_line &line) {
        return view.picked_from != 0 && line.seq >= std::min(view.picked_from, view.picked_to) &&
            line.seq <= std::max(view.picked_from, view.picked_to);
    };
    const auto copy_lines = [&]() {
        const bool any = std::any_of(shown.begin(), shown.end(), [&](const rv_editor_log_line *l) { return picked(*l); });
        std::string text;
        for (const rv_editor_log_line *line : shown) {
            if (!any || picked(*line)) {
                text += rv_editor_output_text(*line);
            }
        }
        ImGui::SetClipboardText(text.c_str());
    };
    if (copy) {
        copy_lines();
    }
    if (exporting) {
        (void)rv_editor_output_export(app, shown, view.exported);
    }
    if (!view.exported.empty()) {
        rv_editor_path_row(rv_editor_text("pane_output.label_exported"), view.exported, theme);
    }

    // The lines draw in the code font; the header takes its cell width from it.
    rv_editor_font_code_push();
    const float cell = ImGui::CalcTextSize(char_width_sample.data()).x;
    rv_editor_output_header(view, cell, theme);
    const float at_level = cell * view.columns[0];
    const float at_source = at_level + cell * view.columns[1];
    const float at_message = at_source + cell * view.columns[message_column_index];

    rv_editor_log_begin("##lines", ImVec2(0, 0), theme);
    const float line_h = ImGui::GetTextLineHeight();
    // Less the half cell each row starts in by, and as much again on the right.
    const float wrap_w = std::max(cell * wrap_width_min_cells, ImGui::GetContentRegionAvail().x - at_message - cell);
    // A monospace line's rows: its characters over the width, give or take a word
    // carried whole. An estimate for lines out of sight, exact for the rest.
    const auto rows_of = [&](const rv_editor_log_line &line) {
        const size_t chars = static_cast<size_t>(std::count_if(line.text.begin(), line.text.end(),
            [](char c) {
                return (static_cast<unsigned char>(c) & utf8_byte_type_mask) != utf8_continuation_marker;
            }));
        const float per_row = std::max(1.0f, std::floor(wrap_w / cell));
        return std::max(1.0f, std::ceil(static_cast<float>(chars) / per_row));
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    const uint32_t code_surface_col = rv_editor_col(theme.code_surface);
    ImGui::PushStyleColor(ImGuiCol_Header, code_surface_col);
    const uint32_t hovered_alpha_channel = header_hovered_alpha << IM_COL32_A_SHIFT;
    const uint32_t hovered_color = (code_surface_col & ~IM_COL32_A_MASK) | hovered_alpha_channel;
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hovered_color);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, code_surface_col);
    const auto row = [&](const rv_editor_log_line &line) {
        const float h = view.wrap
            ? std::max(line_h, ImGui::CalcTextSize(line.text.c_str(), nullptr, false, wrap_w).y)
            : line_h;
        const ImVec2 start = ImGui::GetCursorPos();
        const float x = start.x + cell * half_cell;
        ImGui::PushID(static_cast<int>(line.seq));
        if (ImGui::Selectable("##row", picked(line), ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, h))) {
            if (ImGui::GetIO().KeyShift && view.picked_from != 0) {
                view.picked_to = line.seq;
            } else {
                view.picked_from = view.picked_to = line.seq;
            }
            view.follow = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", rv_editor_output_origin(line).c_str());
        }
        ImGui::PopID();
        ImGui::SetCursorPos(ImVec2(x, start.y));
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_subtext));
        ImGui::TextUnformatted(rv_editor_log_stamp(line, false).c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(x + at_level);
        const uint32_t level_ink = line.level == rv_editor_log_level::error ? theme.code_red
            : line.level == rv_editor_log_level::warning                  ? theme.code_yellow
                                                                          : theme.code_blue;
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(level_ink));
        ImGui::TextUnformatted(rv_editor_output_level_text(line.level));
        ImGui::PopStyleColor();
        ImGui::SameLine(x + at_source);
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_subtext));
        ImGui::TextUnformatted(rv_editor_log_source_name(line.source));
        ImGui::PopStyleColor();
        ImGui::SameLine(x + at_message);
        if (view.wrap) {
            // Continuation rows start under the message, not under the time.
            ImGui::PushTextWrapPos(x + at_message + wrap_w);
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextUnformatted(line.text.c_str());
        }
        ImGui::SetCursorPos(start);
        ImGui::Dummy(ImVec2(0.0f, h));
    };
    if (view.wrap) {
        // Rows of different heights: those out of sight only take their room.
        const float top = ImGui::GetScrollY();
        const float bottom = top + ImGui::GetWindowHeight();
        for (const rv_editor_log_line *line : shown) {
            const float y = ImGui::GetCursorPosY();
            const float guess = rows_of(*line) * line_h;
            if (y + guess < top || y > bottom) {
                ImGui::Dummy(ImVec2(0.0f, guess));
                continue;
            }
            row(*line);
        }
    } else {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                row(*shown[static_cast<size_t>(i)]);
            }
        }
    }
    ImGui::PopStyleColor(pushed_style_colors);
    ImGui::PopStyleVar();
    if (ImGui::IsWindowFocused() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
        copy_lines();
    }
    view.scroll_x = ImGui::GetScrollX();
    // Scrolling up by hand stops following; a new line never takes the keyboard.
    if (view.follow && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.0f) {
        view.follow = false;
    }
    if (view.follow && ImGui::GetScrollY() < ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    rv_editor_log_end(theme);
    rv_editor_font_code_pop();
    rv_editor_well_end();
}

} // namespace rv_editor
