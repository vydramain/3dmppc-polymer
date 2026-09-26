// Output, Runtime Log and Build Log (LOG-01..03): one component over the shared
// log, each view with its own sources, search, follow and wrap. Clear View hides
// what is there now; it deletes nothing.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

std::string rv_editor_output_stamp(const rv_editor_log_line &line)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld.%03lld", static_cast<long long>(line.ms / 60000),
        static_cast<long long>(line.ms / 1000 % 60), static_cast<long long>(line.ms % 1000));
    return buf;
}

const char *rv_editor_output_level(rv_editor_log_level level)
{
    return level == rv_editor_log_level::error ? "ERR" : level == rv_editor_log_level::warning ? "WRN" : "INF";
}

std::string rv_editor_output_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The view's own choice of lines: sources, search, and what Clear View put behind it.
bool rv_editor_output_passes(const rv_editor_output_view &view, const rv_editor_log_line &line, const std::string &needle)
{
    if (!view.show[static_cast<size_t>(line.source)]) {
        return false;
    }
    return needle.empty() || rv_editor_output_lower(line.text).find(needle) != std::string::npos;
}

// The line as Copy and Export write it.
std::string rv_editor_output_text(const rv_editor_log_line &line)
{
    return rv_editor_output_stamp(line) + " " + rv_editor_output_level(line.level) + " " +
        rv_editor_log_source_name(line.source) + " " + line.text + "\n";
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

} // namespace

void rv_editor_pane_output(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    rv_editor_output_view &view = app.outputs[pane];
    const rv_editor_log &log = app.log;

    // Which sources, named on the button so a filter is never out of sight; the
    // menu has a way back to all of them.
    std::string sources;
    for (size_t i = 0; i < view.show.size(); ++i) {
        if (view.show[i]) {
            sources += (sources.empty() ? "" : ", ") + std::string(rv_editor_log_source_name(static_cast<rv_editor_log_source>(i)));
        }
    }
    const std::string sources_label = "Sources: " + (sources.empty() ? std::string("none") : sources) + "##sources";
    if (rv_editor_button(sources_label.c_str(), theme)) {
        ImGui::OpenPopup("##sources_menu");
    }
    rv_editor_menu_style_push();
    if (ImGui::BeginPopup("##sources_menu")) {
        for (size_t i = 0; i < view.show.size(); ++i) {
            bool on = view.show[i];
            if (ImGui::MenuItem(rv_editor_log_source_name(static_cast<rv_editor_log_source>(i)), nullptr, &on)) {
                view.show[i] = on;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Filters")) {
            view.show = { true, true, true, false };
            view.search[0] = '\0';
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();
    rv_editor_flow(ImGui::GetFontSize() * 10.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    rv_editor_text_field("##search", view.search, sizeof(view.search), theme);
    ImGui::SetItemTooltip("Search: shows only lines containing this text");
    rv_editor_flow(rv_editor_checkbox_width("Wrap"));
    rv_editor_checkbox("Wrap", &view.wrap, theme);
    if (!view.follow) {
        rv_editor_flow(rv_editor_button_width("Latest"));
        if (rv_editor_button("Latest", theme)) {
            view.follow = true;
        }
        ImGui::SetItemTooltip("Back to the newest line, and keep following");
    }
    rv_editor_flow(rv_editor_button_width("Copy"));
    const bool copy = rv_editor_button("Copy", theme);
    rv_editor_flow(rv_editor_button_width("Clear View"));
    if (rv_editor_button("Clear View", theme)) {
        view.hide_before = log.revision() + 1;
    }
    ImGui::SetItemTooltip("Hides the lines shown now in this view; the log keeps them");
    rv_editor_flow(rv_editor_button_width("Export"));
    const bool exporting = rv_editor_button("Export", theme,
        { rv_editor_look::live, app.project.open ? nullptr : "No project: exports go to its cache directory" });

    // The lines, and what the filters keep out of sight (LOG-03).
    const std::string needle = rv_editor_output_lower(view.search);
    std::vector<const rv_editor_log_line *> shown;
    size_t hidden_errors = 0;
    size_t hidden_warnings = 0;
    for (const rv_editor_log_line &line : log.lines()) {
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
    if (copy) {
        std::string text;
        for (const rv_editor_log_line *line : shown) {
            text += rv_editor_output_text(*line);
        }
        ImGui::SetClipboardText(text.c_str());
    }
    if (exporting) {
        rv_editor_output_export(app, shown, view.exported);
    }
    if (!view.exported.empty()) {
        rv_editor_path_row("Exported", view.exported, theme);
    }

    // Columns at fixed places in the code font: Time, Level, Source, Message.
    rv_editor_font_code_push();
    const float cell = ImGui::CalcTextSize("0").x;
    const float at_level = cell * 11.0f;
    const float at_source = cell * 15.0f;
    const float at_message = cell * 25.0f;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const float header_x = ImGui::GetCursorPosX() + ImGui::GetStyle().WindowPadding.x;
    ImGui::SetCursorPosX(header_x);
    ImGui::TextUnformatted("Time");
    ImGui::SameLine(header_x + at_level);
    ImGui::TextUnformatted("Lvl");
    ImGui::SameLine(header_x + at_source);
    ImGui::TextUnformatted("Source");
    ImGui::SameLine(header_x + at_message);
    ImGui::TextUnformatted("Message");
    ImGui::PopStyleColor();

    rv_editor_log_begin("##lines", ImVec2(0, 0), theme);
    const auto row = [&](const rv_editor_log_line &line) {
        const float x = ImGui::GetCursorPosX();
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_subtext));
        ImGui::TextUnformatted(rv_editor_output_stamp(line).c_str());
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
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextUnformatted(line.text.c_str());
        }
    };
    if (view.wrap) {
        // Rows of different heights: every one is laid out.
        for (const rv_editor_log_line *line : shown) {
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
    // Scrolling up by hand stops following until Latest (LOG-02); a new line never takes the keyboard.
    if (view.follow && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.0f) {
        view.follow = false;
    }
    if (view.follow && ImGui::GetScrollY() < ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    rv_editor_log_end(theme);
    rv_editor_font_code_pop();
}

} // namespace rv_editor
