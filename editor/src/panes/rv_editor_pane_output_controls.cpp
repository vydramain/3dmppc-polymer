// The control row of the log panes (Console Output, Runtime Log, Build Log): Source,
// Level, Find, Follow and Wrap, then Copy, Export and Clear View, all in one row.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_table.hpp"

namespace rv_editor
{

namespace
{

// Dropdown menu label separator before ImGui ID.
constexpr std::string_view dropdown_id_marker = "   ###";

// Find field width bounds (multiples of font size, in em).
constexpr float find_width_min_em = 6.0f;
constexpr float find_width_max_em = 12.0f;

const char *rv_editor_output_level_filter(rv_editor_log_level level)
{
    if (level == rv_editor_log_level::error) {
        return rv_editor_text("pane_output_controls.level_errors");
    }
    if (level == rv_editor_log_level::warning) {
        return rv_editor_text("pane_output_controls.level_warnings");
    }
    return rv_editor_text("pane_output_controls.level_all");
}

// A button that opens a menu: "Source: All" with a down arrow at its right end.
std::string rv_editor_output_drop_label(const std::string &text, const char *id)
{
    return text + dropdown_id_marker.data() + id;
}

// Every process with a line in the log now, newest first: pid, run number and label.
struct rv_editor_output_run {
    int64_t pid;
    std::string label;
};

std::vector<rv_editor_output_run> rv_editor_output_runs(const rv_editor_log &log)
{
    std::vector<int64_t> pids;
    for (auto it = log.lines().rbegin(); it != log.lines().rend(); ++it) {
        if (it->pid == 0 || std::find(pids.begin(), pids.end(), it->pid) != pids.end()) {
            continue;
        }
        pids.push_back(it->pid);
    }
    std::vector<rv_editor_output_run> runs;
    for (const int64_t pid : pids) {
        bool has_build = false;
        bool has_candidate = false;
        uint32_t run = 0;
        for (const rv_editor_log_line &line : log.lines()) {
            if (line.pid != pid) {
                continue;
            }
            has_build = has_build || line.source == rv_editor_log_source::build;
            has_candidate = has_candidate || line.source == rv_editor_log_source::candidate;
            run = line.run;
        }
        const char *kind;
        if (has_build) {
            kind = rv_editor_text("pane_output_controls.source_build");
        } else if (has_candidate) {
            kind = rv_editor_text("pane_output_controls.source_candidate");
        } else {
            kind = rv_editor_text("pane_output_controls.source_session");
        }
        const std::string label =
            rv_editor_text_format("pane_output_controls.run_label", std::make_format_args(kind, run, pid));
        runs.push_back({ pid, label });
    }
    return runs;
}

// A drop button of one width whatever it says: as wide as the widest of `widest`; a text that does not fit ends in
// "..." with the whole of it in a tooltip. The id marker keeps the button's id steady as the text changes.
bool rv_editor_output_drop(const std::string &text,
    const char *id,
    const std::array<std::string, 3> &widest,
    const rv_editor_theme &theme)
{
    const std::array<std::string, 3> w = { rv_editor_output_drop_label(widest[0], id),
        rv_editor_output_drop_label(widest[1], id),
        rv_editor_output_drop_label(widest[2], id) };
    const float side = ImGui::GetFrameHeight();
    const float room = rv_editor_button_width_for({ w[0].c_str(), w[1].c_str(), w[2].c_str() }) -
        ImGui::GetStyle().FramePadding.x * 2.0f - side;
    const std::string fitted = rv_editor_ellipsize(text.c_str(), room);
    const std::string label = rv_editor_output_drop_label(fitted, id);
    const bool clicked = rv_editor_button_fixed(label.c_str(), { w[0].c_str(), w[1].c_str(), w[2].c_str() }, theme);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 max = ImGui::GetItemRectMax();
    rv_editor_draw_arrow(ImGui::GetWindowDrawList(),
        ImVec2(max.x - side, ImGui::GetItemRectMin().y),
        max,
        theme,
        ImGuiDir_Down,
        theme.text);
    if (hovered && fitted != text) {
        rv_editor_tooltip(text.c_str());
    }
    return clicked;
}

// The three texts the Source button and the Level button each take the widest of.
std::array<std::string, 3> rv_editor_output_source_widest()
{
    const std::string prefix = rv_editor_text("pane_output_controls.dropdown_source");
    return { prefix + rv_editor_text("pane_output.sources_all_protocol"),
        prefix + rv_editor_text("pane_output.sources_all_protocol"),
        prefix + rv_editor_text("pane_output.sources_none") };
}

std::array<std::string, 3> rv_editor_output_level_widest()
{
    const std::string prefix = rv_editor_text("pane_output_controls.dropdown_level");
    return { prefix + rv_editor_text("pane_output_controls.level_all"),
        prefix + rv_editor_text("pane_output_controls.level_warnings"),
        prefix + rv_editor_text("pane_output_controls.level_errors") };
}

float rv_editor_output_drop_width(const std::array<std::string, 3> &widest, const char *id)
{
    const std::array<std::string, 3> w = { rv_editor_output_drop_label(widest[0], id),
        rv_editor_output_drop_label(widest[1], id),
        rv_editor_output_drop_label(widest[2], id) };
    return rv_editor_button_width_for({ w[0].c_str(), w[1].c_str(), w[2].c_str() });
}

// Follow, Wrap, Copy, Export and Clear View, each with the gap before it.
float rv_editor_output_tail_width()
{
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    return rv_editor_checkbox_width(rv_editor_text("pane_output_controls.button_follow")) +
        rv_editor_checkbox_width(rv_editor_text("pane_output_controls.button_wrap")) +
        rv_editor_button_width(rv_editor_text("pane_output_controls.button_copy")) +
        rv_editor_button_width(rv_editor_text("pane_output_controls.button_export")) +
        rv_editor_button_width(rv_editor_text("pane_output_controls.button_clear_view")) + 5.0f * gap;
}

} // namespace

float rv_editor_output_controls_width()
{
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    return rv_editor_output_drop_width(rv_editor_output_source_widest(), "sources") + gap +
        rv_editor_output_drop_width(rv_editor_output_level_widest(), "level") + gap + ImGui::GetFontSize() * find_width_min_em +
        rv_editor_output_tail_width();
}

// Source and Level menus, Find, Follow, Wrap, Copy, Export and Clear View, every one always shown: the tile's
// minimum is as wide as the row (rv_editor_output_controls_width). Find takes the room that is left, between its
// minimum and maximum. Everything here draws in the UI font, like the rest of the editor.
void rv_editor_output_controls(rv_editor_app &app,
    rv_editor_output_view &view,
    bool &copy,
    bool &exporting,
    const rv_editor_theme &theme)
{
    const rv_editor_log &log = app.log;
    const auto clear_view = [&]() {
        view.hide_before = log.revision() + 1;
        view.picked_from = view.picked_to = 0;
    };

    const std::string source_prefix = rv_editor_text("pane_output_controls.dropdown_source");
    const std::string source_label = view.run_pid != 0 ? view.run_label : rv_editor_output_sources(view, false);
    const std::string source_drop = source_prefix + source_label;
    if (rv_editor_output_drop(source_drop, "sources", rv_editor_output_source_widest(), theme)) {
        ImGui::OpenPopup("##sources_menu");
    }
    if (rv_editor_popup_begin("##sources_menu")) {
        for (size_t i = 0; i < view.show.size(); ++i) {
            bool on = view.show[i];
            if (rv_editor_menu_check(rv_editor_log_source_name(static_cast<rv_editor_log_source>(i)), nullptr, &on, nullptr)) {
                view.show[i] = on;
            }
        }
        rv_editor_separator();
        if (rv_editor_menu_choice(rv_editor_text("pane_output_controls.menu_all_runs"), nullptr, view.run_pid == 0, nullptr)) {
            view.run_pid = 0;
            view.run_label.clear();
        }
        for (const rv_editor_output_run &run : rv_editor_output_runs(log)) {
            if (rv_editor_menu_choice(run.label.c_str(), nullptr, view.run_pid == run.pid, nullptr)) {
                view.run_pid = run.pid;
                view.run_label = run.label;
            }
        }
        rv_editor_separator();
        if (rv_editor_menu_item(rv_editor_text("pane_output_controls.menu_reset_filters"), nullptr, nullptr)) {
            view.show = {};
            view.show[static_cast<size_t>(rv_editor_log_source::editor)] = true;
            view.show[static_cast<size_t>(rv_editor_log_source::build)] = true;
            view.show[static_cast<size_t>(rv_editor_log_source::candidate)] = true;
            view.show[static_cast<size_t>(rv_editor_log_source::runtime)] = true;
            view.level = rv_editor_log_level::info;
            view.search[0] = '\0';
            view.run_pid = 0;
            view.run_label.clear();
        }
        rv_editor_popup_end();
    }
    ImGui::SameLine();

    const std::string level_prefix = rv_editor_text("pane_output_controls.dropdown_level");
    const std::string level = level_prefix + rv_editor_output_level_filter(view.level);
    if (rv_editor_output_drop(level, "level", rv_editor_output_level_widest(), theme)) {
        ImGui::OpenPopup("##level_menu");
    }
    if (rv_editor_popup_begin("##level_menu")) {
        for (const rv_editor_log_level l :
            { rv_editor_log_level::info, rv_editor_log_level::warning, rv_editor_log_level::error }) {
            if (rv_editor_menu_choice(rv_editor_output_level_filter(l), nullptr, view.level == l, nullptr)) {
                view.level = l;
            }
        }
        rv_editor_popup_end();
    }
    ImGui::SameLine();

    const float find_min = ImGui::GetFontSize() * find_width_min_em;
    const float find_max = ImGui::GetFontSize() * find_width_max_em;
    const float find_w = std::clamp(ImGui::GetContentRegionAvail().x - rv_editor_output_tail_width(), find_min, find_max);

    // Find, with its purpose written in it while it is empty.
    ImGui::SetNextItemWidth(find_w);
    rv_editor_text_field("##search", view.search, sizeof(view.search), theme);
    if (view.search[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + pad.x, min.y + pad.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            rv_editor_text("pane_output_controls.placeholder_find"));
    }
    rv_editor_item_tooltip(rv_editor_text("pane_output_controls.tooltip_find"));

    ImGui::SameLine();
    if (rv_editor_checkbox(rv_editor_text("pane_output_controls.button_follow"), &view.follow, theme)) {
        view.picked_from = view.picked_to = 0;
    }
    rv_editor_item_tooltip(rv_editor_text("pane_output_controls.tooltip_follow"));
    ImGui::SameLine();
    rv_editor_checkbox(rv_editor_text("pane_output_controls.button_wrap"), &view.wrap, theme);
    ImGui::SameLine();
    copy = rv_editor_button(rv_editor_text("pane_output_controls.button_copy"), theme);
    rv_editor_item_tooltip(rv_editor_text("pane_output_controls.tooltip_copy"));
    ImGui::SameLine();
    exporting = rv_editor_button(rv_editor_text("pane_output_controls.button_export"),
        theme,
        { rv_editor_look::live, app.project.open ? nullptr : rv_editor_text("pane_output_controls.tooltip_no_project") });
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_output_controls.button_clear_view"), theme)) {
        clear_view();
    }
    rv_editor_item_tooltip(rv_editor_text("pane_output_controls.tooltip_clear_view"));
}

} // namespace rv_editor
