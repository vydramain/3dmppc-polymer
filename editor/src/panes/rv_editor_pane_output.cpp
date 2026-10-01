// Output, Runtime Log and Build Log (LOG-01..03): one component over the shared
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

#include "font/rv_editor_font.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

const char *rv_editor_output_level(rv_editor_log_level level)
{
    return level == rv_editor_log_level::error ? "ERR" : level == rv_editor_log_level::warning ? "WRN" : "INF";
}

const char *rv_editor_output_kind(rv_editor_log_source source)
{
    switch (source) {
    case rv_editor_log_source::build:
        return "build";
    case rv_editor_log_source::candidate:
        return "candidate";
    default:
        return "session"; // runtime and protocol
    }
}

// "pid N, <kind> #run, stdout|stderr", or "the editor" for the editor's own lines.
std::string rv_editor_output_origin(const rv_editor_log_line &line)
{
    if (line.pid == 0) {
        return "the editor";
    }
    std::string out = "pid " + std::to_string(line.pid) + ", " + rv_editor_output_kind(line.source) + " #" +
        std::to_string(line.run);
    if (line.channel == rv_editor_log_channel::out) {
        out += ", stdout";
    } else if (line.channel == rv_editor_log_channel::err) {
        out += ", stderr";
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
    std::string channel = line.channel == rv_editor_log_channel::out ? " out"
        : line.channel == rv_editor_log_channel::err ? " err" : "";
    return rv_editor_log_stamp(line, true) + " " + rv_editor_output_level(line.level) + " " +
        rv_editor_log_source_name(line.source) + " pid " + std::to_string(line.pid) + channel + " " + line.text + "\n";
}

bool rv_editor_output_export(rv_editor_app &app, const std::vector<const rv_editor_log_line *> &shown,
    std::string &where)
{
    const std::filesystem::path dir = app.project.cache_dir / "logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / ("log-" + std::to_string(std::time(nullptr)) + ".txt");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const rv_editor_log_line *line : shown) {
        out << rv_editor_output_text(*line);
    }
    out.close();
    where = out ? path.string() : "cannot write " + path.string();
    return static_cast<bool>(out);
}

// Time, Lvl, Source and Message over the lines, in the UI font but at the cells'
// code-font x positions below; the borders between them drag.
void rv_editor_output_header(rv_editor_output_view &view, float cell, const rv_editor_theme &theme)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight() + 4.0f;
    const float w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), rv_editor_col(theme.code_base));
    dl->AddLine(ImVec2(p0.x, p0.y + h - 1.0f), ImVec2(p0.x + w, p0.y + h - 1.0f), rv_editor_col(theme.code_surface));
    dl->PushClipRect(p0, ImVec2(p0.x + w, p0.y + h), true);
    constexpr const char *names[] = { "Time", "Lvl", "Source", "Message" };
    ImFont *ui_font = rv_editor_font_ui();
    const float ty = p0.y + (h - ui_font->LegacySize) * 0.5f;
    float x = p0.x + cell * 0.5f - view.scroll_x;
    for (size_t i = 0; i < 4; ++i) {
        dl->AddText(ui_font, ui_font->LegacySize, ImVec2(x, ty), rv_editor_col(theme.code_text), names[i]);
        if (i == 3) {
            break;
        }
        x += view.columns[i] * cell;
        const float border = x - cell * 0.5f;
        dl->AddLine(ImVec2(border, p0.y + 2.0f), ImVec2(border, p0.y + h - 3.0f), rv_editor_col(theme.code_surface));
        ImGui::SetCursorScreenPos(ImVec2(border - 3.0f, p0.y));
        ImGui::PushID(static_cast<int>(i));
        ImGui::InvisibleButton("##border", ImVec2(6.0f, h));
        ImGui::PopID();
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            view.columns[i] = std::max(4.0f, view.columns[i] + ImGui::GetIO().MouseDelta.x / cell);
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
        return view.show[static_cast<size_t>(rv_editor_log_source::protocol)] ? "All + protocol" : "All";
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
        out += (out.empty() ? "" : ", ") + name;
    }
    return out.empty() ? "None" : out;
}

std::string rv_editor_output_title(const rv_editor_app &app, rv_editor_pane_id pane)
{
    const auto it = app.outputs.find(pane);
    if (it == app.outputs.end()) {
        return "Console Output: All";
    }
    const rv_editor_output_view &view = it->second;
    if (view.run_pid != 0) {
        return "Console Output: " + view.run_label;
    }
    std::string what = rv_editor_output_sources(view, true);
    if (what == "Build" && app.build.number() != 0) {
        what += " #" + std::to_string(app.build.number());
    }
    if (view.level != rv_editor_log_level::info) {
        what += view.level == rv_editor_log_level::error ? ", errors" : ", warnings+";
    }
    return "Console Output: " + what;
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

    // The lines, and what the filters keep out of sight (LOG-03).
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
        rv_editor_status(("No lines from " + view.run_label + " are kept").c_str(), rv_editor_status_kind::idle, theme);
    }
    if (hidden_errors + hidden_warnings != 0) {
        const std::string hidden = std::to_string(hidden_errors) + " errors, " + std::to_string(hidden_warnings) +
            " warnings hidden by the filters";
        rv_editor_status(hidden.c_str(), hidden_errors != 0 ? rv_editor_status_kind::error : rv_editor_status_kind::warning,
            theme);
    }
    if (log.dropped() != 0) {
        rv_editor_flow(ImGui::GetFontSize() * 16.0f);
        ImGui::Text("%llu oldest lines dropped", static_cast<unsigned long long>(log.dropped()));
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
        rv_editor_output_export(app, shown, view.exported);
    }
    if (!view.exported.empty()) {
        rv_editor_path_row("Exported", view.exported, theme);
    }

    // The lines draw in the code font; the header takes its cell width from it.
    rv_editor_font_code_push();
    const float cell = ImGui::CalcTextSize("0").x;
    rv_editor_output_header(view, cell, theme);
    const float at_level = cell * view.columns[0];
    const float at_source = at_level + cell * view.columns[1];
    const float at_message = at_source + cell * view.columns[2];

    rv_editor_log_begin("##lines", ImVec2(0, 0), theme);
    const float line_h = ImGui::GetTextLineHeight();
    // Less the half cell each row starts in by, and as much again on the right.
    const float wrap_w = std::max(cell * 8.0f, ImGui::GetContentRegionAvail().x - at_message - cell);
    // A monospace line's rows: its characters over the width, give or take a word
    // carried whole. An estimate for lines out of sight, exact for the rest.
    const auto rows_of = [&](const rv_editor_log_line &line) {
        const size_t chars = static_cast<size_t>(std::count_if(line.text.begin(), line.text.end(),
            [](char c) { return (static_cast<unsigned char>(c) & 0xc0) != 0x80; }));
        const float per_row = std::max(1.0f, std::floor(wrap_w / cell));
        return std::max(1.0f, std::ceil(static_cast<float>(chars) / per_row));
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, rv_editor_col(theme.code_surface));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, (rv_editor_col(theme.code_surface) & ~IM_COL32_A_MASK) | (0x60u << IM_COL32_A_SHIFT));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, rv_editor_col(theme.code_surface));
    const auto row = [&](const rv_editor_log_line &line) {
        const float h = view.wrap
            ? std::max(line_h, ImGui::CalcTextSize(line.text.c_str(), nullptr, false, wrap_w).y)
            : line_h;
        const ImVec2 start = ImGui::GetCursorPos();
        const float x = start.x + cell * 0.5f;
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
        ImGui::TextUnformatted(rv_editor_output_level(line.level));
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
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (ImGui::IsWindowFocused() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
        copy_lines();
    }
    view.scroll_x = ImGui::GetScrollX();
    // Scrolling up by hand stops following (LOG-02); a new line never takes the keyboard.
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
