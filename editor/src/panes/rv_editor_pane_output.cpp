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

const char *rv_editor_output_level_filter(rv_editor_log_level level)
{
    return level == rv_editor_log_level::error ? "Errors" : level == rv_editor_log_level::warning ? "Warnings+" : "All";
}

std::string rv_editor_output_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "All" (every source but the protocol trace), "All + protocol", or the names shown.
std::string rv_editor_output_sources(const rv_editor_output_view &view, bool capital)
{
    if (view.show[0] && view.show[1] && view.show[2]) {
        return view.show[3] ? "All + protocol" : "All";
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

// The view's own choice of lines: sources, level, search.
bool rv_editor_output_passes(const rv_editor_output_view &view, const rv_editor_log_line &line, const std::string &needle)
{
    if (!view.show[static_cast<size_t>(line.source)] || line.level < view.level) {
        return false;
    }
    return needle.empty() || rv_editor_output_lower(line.text).find(needle) != std::string::npos;
}

// The line as Copy and Export write it.
std::string rv_editor_output_text(const rv_editor_log_line &line)
{
    return rv_editor_log_stamp(line, true) + " " + rv_editor_output_level(line.level) + " " +
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

// A button that opens a menu: "Source: All" with a down arrow at its right end.
std::string rv_editor_output_drop_label(const std::string &text, const char *id)
{
    return text + "   ##" + id;
}

bool rv_editor_output_drop(const std::string &label, const rv_editor_theme &theme)
{
    const bool clicked = rv_editor_button(label.c_str(), theme);
    const ImVec2 max = ImGui::GetItemRectMax();
    const float side = ImGui::GetItemRectSize().y;
    rv_editor_draw_arrow(ImGui::GetWindowDrawList(), ImVec2(max.x - side, ImGui::GetItemRectMin().y), max, theme,
        ImGuiDir_Down, theme.text);
    return clicked;
}

// Source and Level menus, Find, Follow and Wrap; Copy, Export and Clear View at the right.
void rv_editor_output_controls(rv_editor_app &app, rv_editor_output_view &view, bool &copy, bool &exporting,
    const rv_editor_theme &theme)
{
    const rv_editor_log &log = app.log;
    if (rv_editor_output_drop(rv_editor_output_drop_label("Source: " + rv_editor_output_sources(view, false), "sources"),
            theme)) {
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
            view.level = rv_editor_log_level::info;
            view.search[0] = '\0';
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();

    const std::string level =
        rv_editor_output_drop_label(std::string("Level: ") + rv_editor_output_level_filter(view.level), "level");
    rv_editor_flow(rv_editor_button_width(level.c_str()));
    if (rv_editor_output_drop(level, theme)) {
        ImGui::OpenPopup("##level_menu");
    }
    rv_editor_menu_style_push();
    if (ImGui::BeginPopup("##level_menu")) {
        for (const rv_editor_log_level l :
            { rv_editor_log_level::info, rv_editor_log_level::warning, rv_editor_log_level::error }) {
            if (ImGui::MenuItem(rv_editor_output_level_filter(l), nullptr, view.level == l)) {
                view.level = l;
            }
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();

    // Find, with its purpose written in it while it is empty.
    const float find = ImGui::GetFontSize() * 12.0f;
    rv_editor_flow(find);
    ImGui::SetNextItemWidth(find);
    rv_editor_text_field("##search", view.search, sizeof(view.search), theme);
    if (view.search[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + pad.x, min.y + pad.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), "Find...");
    }
    ImGui::SetItemTooltip("Shows only lines containing this text");
    rv_editor_flow(rv_editor_checkbox_width("Follow"));
    if (rv_editor_checkbox("Follow", &view.follow, theme)) {
        view.picked_from = view.picked_to = 0;
    }
    ImGui::SetItemTooltip("Keep the newest line in sight; scrolling up turns it off");
    rv_editor_flow(rv_editor_checkbox_width("Wrap"));
    rv_editor_checkbox("Wrap", &view.wrap, theme);

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float group = rv_editor_button_width("Copy") + rv_editor_button_width("Export") +
        rv_editor_button_width("Clear View") + gap * 2.0f;
    rv_editor_flow(group);
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - group));
    copy = rv_editor_button("Copy", theme);
    ImGui::SetItemTooltip("The selected lines, or every line shown when none is selected");
    ImGui::SameLine();
    exporting = rv_editor_button("Export", theme,
        { rv_editor_look::live, app.project.open ? nullptr : "No project: exports go to its cache directory" });
    ImGui::SameLine();
    if (rv_editor_button("Clear View", theme)) {
        view.hide_before = log.revision() + 1;
        view.picked_from = view.picked_to = 0;
    }
    ImGui::SetItemTooltip("Hides the lines shown now in this view; the log keeps them");
}

// Time, Level, Source and Message over the lines, in the lines' font and at their
// places; the borders between them drag.
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
    const float ty = p0.y + 2.0f;
    float x = p0.x + cell * 0.5f - view.scroll_x;
    for (size_t i = 0; i < 4; ++i) {
        dl->AddText(ImVec2(x, ty), rv_editor_col(theme.code_text), names[i]);
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

std::string rv_editor_output_title(const rv_editor_app &app, rv_editor_pane_id pane)
{
    const auto it = app.outputs.find(pane);
    if (it == app.outputs.end()) {
        return "Console Output: All";
    }
    const rv_editor_output_view &view = it->second;
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

    // The controls and the lines share the code font, so the pane reads as one table.
    rv_editor_font_code_push();
    bool copy = false;
    bool exporting = false;
    rv_editor_output_controls(app, view, copy, exporting, theme);

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
    // carried whole. ponytail: an estimate for lines out of sight, exact for the rest.
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
}

} // namespace rv_editor
