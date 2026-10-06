// The control row of the log panes (Console Output, Runtime Log, Build Log): Source,
// Level, Find, Follow and Wrap, then Copy, Export and Clear View.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Dropdown menu label separator before ImGui ID.
constexpr std::string_view dropdown_id_marker = "   ##";

// Tail buttons: Follow, Wrap, Copy, Export, Clear View (5 items).
constexpr size_t tail_buttons_count = 5;

// Tail button indices: positions in tail_w and hidden arrays.
constexpr size_t tail_follow = 0;
constexpr size_t tail_wrap = 1;
constexpr size_t tail_copy = 2;
constexpr size_t tail_export = 3;
constexpr size_t tail_clear_view = 4;

// Drop order: indices to hide when space tight (Clear View, Export, Copy, Wrap, Follow).
constexpr size_t tail_buttons_drop_order[tail_buttons_count] = {
    tail_clear_view, tail_export, tail_copy, tail_wrap, tail_follow
};

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
struct rv_editor_output_run
{
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
        const std::string label = rv_editor_text_format("pane_output_controls.run_label",
            std::make_format_args(kind, run, pid));
        runs.push_back({ pid, label });
    }
    return runs;
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

} // namespace

// Source and Level menus, Find, Follow and Wrap; Copy, Export and Clear View at the
// right. Source, Level and Find (down to a minimum) always stay; the rest hide behind
// a "More" menu, in this order, when the row is too narrow: Clear View, Export, Copy,
// Wrap, Follow. Everything here draws in the UI font, like the rest of the editor.
void rv_editor_output_controls(rv_editor_app &app, rv_editor_output_view &view, bool &copy, bool &exporting,
    const rv_editor_theme &theme)
{
    const rv_editor_log &log = app.log;
    const auto clear_view = [&]() {
        view.hide_before = log.revision() + 1;
        view.picked_from = view.picked_to = 0;
    };

    const std::string source_label = view.run_pid != 0 ? view.run_label : rv_editor_output_sources(view, false);
    const std::string source_drop = std::string(rv_editor_text("pane_output_controls.dropdown_source")) +
        source_label;
    if (rv_editor_output_drop(rv_editor_output_drop_label(source_drop, "sources"), theme)) {
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
        if (ImGui::MenuItem(rv_editor_text("pane_output_controls.menu_all_runs"), nullptr, view.run_pid == 0)) {
            view.run_pid = 0;
            view.run_label.clear();
        }
        for (const rv_editor_output_run &run : rv_editor_output_runs(log)) {
            if (ImGui::MenuItem(run.label.c_str(), nullptr, view.run_pid == run.pid)) {
                view.run_pid = run.pid;
                view.run_label = run.label;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(rv_editor_text("pane_output_controls.menu_reset_filters"))) {
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
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();
    ImGui::SameLine();

    const std::string level = rv_editor_output_drop_label(
        std::string(rv_editor_text("pane_output_controls.dropdown_level")) +
            rv_editor_output_level_filter(view.level),
        "level");
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
    ImGui::SameLine();

    // Follow, Wrap, Copy, Export, Clear View: dropped in this order (Clear View
    // first, Follow last) behind More, so the row never runs wider than the tile.
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float tail_w[tail_buttons_count] = {
        rv_editor_checkbox_width(rv_editor_text("pane_output_controls.button_follow")),
        rv_editor_checkbox_width(rv_editor_text("pane_output_controls.button_wrap")),
        rv_editor_button_width(rv_editor_text("pane_output_controls.button_copy")),
        rv_editor_button_width(rv_editor_text("pane_output_controls.button_export")),
        rv_editor_button_width(rv_editor_text("pane_output_controls.button_clear_view"))
    };
    const float more_w = rv_editor_button_width(rv_editor_text("pane_output_controls.button_more"));
    bool hidden[tail_buttons_count] = {};
    const auto tail_width = [&](bool any_hidden) {
        float w = 0.0f;
        bool first = true;
        for (size_t i = 0; i < tail_buttons_count; ++i) {
            if (hidden[i]) {
                continue;
            }
            w += (first ? 0.0f : gap) + tail_w[i];
            first = false;
        }
        if (any_hidden) {
            w += (first ? 0.0f : gap) + more_w;
        }
        return w;
    };
    const float find_min = ImGui::GetFontSize() * find_width_min_em;
    const float find_max = ImGui::GetFontSize() * find_width_max_em;
    size_t dropped = 0;
    float find_w = find_max;
    for (;;) {
        const float room = ImGui::GetContentRegionAvail().x - tail_width(dropped != 0);
        if (room >= find_min || dropped == std::size(tail_buttons_drop_order)) {
            find_w = std::clamp(room, find_min, find_max);
            break;
        }
        hidden[tail_buttons_drop_order[dropped]] = true;
        ++dropped;
    }

    // Find, with its purpose written in it while it is empty.
    ImGui::SetNextItemWidth(find_w);
    rv_editor_text_field("##search", view.search, sizeof(view.search), theme);
    if (view.search[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + pad.x, min.y + pad.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), rv_editor_text("pane_output_controls.placeholder_find"));
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_output_controls.tooltip_find"));

    bool any_shown = false;
    if (!hidden[tail_follow]) {
        ImGui::SameLine();
        if (rv_editor_checkbox(rv_editor_text("pane_output_controls.button_follow"), &view.follow, theme)) {
            view.picked_from = view.picked_to = 0;
        }
        ImGui::SetItemTooltip("%s", rv_editor_text("pane_output_controls.tooltip_follow"));
        any_shown = true;
    }
    if (!hidden[tail_wrap]) {
        ImGui::SameLine();
        rv_editor_checkbox(rv_editor_text("pane_output_controls.button_wrap"), &view.wrap, theme);
        any_shown = true;
    }
    if (!hidden[tail_copy]) {
        ImGui::SameLine();
        copy = rv_editor_button(rv_editor_text("pane_output_controls.button_copy"), theme);
        ImGui::SetItemTooltip("%s", rv_editor_text("pane_output_controls.tooltip_copy"));
        any_shown = true;
    }
    if (!hidden[tail_export]) {
        ImGui::SameLine();
        exporting = rv_editor_button(rv_editor_text("pane_output_controls.button_export"), theme,
            { rv_editor_look::live, app.project.open ? nullptr : rv_editor_text("pane_output_controls.tooltip_no_project") });
        any_shown = true;
    }
    if (!hidden[tail_clear_view]) {
        ImGui::SameLine();
        if (rv_editor_button(rv_editor_text("pane_output_controls.button_clear_view"), theme)) {
            clear_view();
        }
        ImGui::SetItemTooltip("%s", rv_editor_text("pane_output_controls.tooltip_clear_view"));
        any_shown = true;
    }
    if (dropped == 0) {
        return;
    }
    if (any_shown) {
        ImGui::SameLine();
    }
    if (rv_editor_button(rv_editor_text("pane_output_controls.button_more"), theme)) {
        ImGui::OpenPopup("##output_more");
    }
    rv_editor_menu_style_push();
    if (ImGui::BeginPopup("##output_more")) {
        if (hidden[tail_follow]) {
            bool on = view.follow;
            if (ImGui::MenuItem(rv_editor_text("pane_output_controls.button_follow"), nullptr, &on)) {
                view.follow = on;
                view.picked_from = view.picked_to = 0;
            }
        }
        if (hidden[tail_wrap]) {
            bool on = view.wrap;
            if (ImGui::MenuItem(rv_editor_text("pane_output_controls.button_wrap"), nullptr, &on)) {
                view.wrap = on;
            }
        }
        if (hidden[tail_copy] && ImGui::MenuItem(rv_editor_text("pane_output_controls.button_copy"))) {
            copy = true;
        }
        if (hidden[tail_export]) {
            const char *disabled = app.project.open ? nullptr : rv_editor_text("pane_output_controls.tooltip_no_project");
            if (ImGui::MenuItem(rv_editor_text("pane_output_controls.button_export"), nullptr, false, disabled == nullptr)) {
                exporting = true;
            }
            if (disabled != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", disabled);
            }
        }
        if (hidden[tail_clear_view] && ImGui::MenuItem(rv_editor_text("pane_output_controls.button_clear_view"))) {
            clear_view();
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();
}

} // namespace rv_editor
