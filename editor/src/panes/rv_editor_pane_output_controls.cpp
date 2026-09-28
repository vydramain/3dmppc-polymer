// The control row of the log panes (Console Output, Runtime Log, Build Log): Source,
// Level, Find, Follow and Wrap, then Copy, Export and Clear View.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

const char *rv_editor_output_level_filter(rv_editor_log_level level)
{
    return level == rv_editor_log_level::error ? "Errors" : level == rv_editor_log_level::warning ? "Warnings+" : "All";
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
            view.show = {};
            view.show[static_cast<size_t>(rv_editor_log_source::editor)] = true;
            view.show[static_cast<size_t>(rv_editor_log_source::build)] = true;
            view.show[static_cast<size_t>(rv_editor_log_source::candidate)] = true;
            view.show[static_cast<size_t>(rv_editor_log_source::runtime)] = true;
            view.level = rv_editor_log_level::info;
            view.search[0] = '\0';
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();
    ImGui::SameLine();

    const std::string level =
        rv_editor_output_drop_label(std::string("Level: ") + rv_editor_output_level_filter(view.level), "level");
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
    const float tail_w[5] = { rv_editor_checkbox_width("Follow"), rv_editor_checkbox_width("Wrap"),
        rv_editor_button_width("Copy"), rv_editor_button_width("Export"), rv_editor_button_width("Clear View") };
    const float more_w = rv_editor_button_width("More");
    constexpr size_t drop_order[5] = { 4, 3, 2, 1, 0 };
    bool hidden[5] = {};
    const auto tail_width = [&](bool any_hidden) {
        float w = 0.0f;
        bool first = true;
        for (size_t i = 0; i < 5; ++i) {
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
    const float find_min = ImGui::GetFontSize() * 6.0f;
    const float find_max = ImGui::GetFontSize() * 12.0f;
    size_t dropped = 0;
    float find_w = find_max;
    for (;;) {
        const float room = ImGui::GetContentRegionAvail().x - tail_width(dropped != 0);
        if (room >= find_min || dropped == std::size(drop_order)) {
            find_w = std::clamp(room, find_min, find_max);
            break;
        }
        hidden[drop_order[dropped]] = true;
        ++dropped;
    }

    // Find, with its purpose written in it while it is empty.
    ImGui::SetNextItemWidth(find_w);
    rv_editor_text_field("##search", view.search, sizeof(view.search), theme);
    if (view.search[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + pad.x, min.y + pad.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), "Find...");
    }
    ImGui::SetItemTooltip("Shows only lines containing this text");

    bool any_shown = false;
    if (!hidden[0]) {
        ImGui::SameLine();
        if (rv_editor_checkbox("Follow", &view.follow, theme)) {
            view.picked_from = view.picked_to = 0;
        }
        ImGui::SetItemTooltip("Keep the newest line in sight; scrolling up turns it off");
        any_shown = true;
    }
    if (!hidden[1]) {
        ImGui::SameLine();
        rv_editor_checkbox("Wrap", &view.wrap, theme);
        any_shown = true;
    }
    if (!hidden[2]) {
        ImGui::SameLine();
        copy = rv_editor_button("Copy", theme);
        ImGui::SetItemTooltip("The selected lines, or every line shown when none is selected");
        any_shown = true;
    }
    if (!hidden[3]) {
        ImGui::SameLine();
        exporting = rv_editor_button("Export", theme,
            { rv_editor_look::live, app.project.open ? nullptr : "No project: exports go to its cache directory" });
        any_shown = true;
    }
    if (!hidden[4]) {
        ImGui::SameLine();
        if (rv_editor_button("Clear View", theme)) {
            clear_view();
        }
        ImGui::SetItemTooltip("Hides the lines shown now in this view; the log keeps them");
        any_shown = true;
    }
    if (dropped == 0) {
        return;
    }
    if (any_shown) {
        ImGui::SameLine();
    }
    if (rv_editor_button("More", theme)) {
        ImGui::OpenPopup("##output_more");
    }
    rv_editor_menu_style_push();
    if (ImGui::BeginPopup("##output_more")) {
        if (hidden[0]) {
            bool on = view.follow;
            if (ImGui::MenuItem("Follow", nullptr, &on)) {
                view.follow = on;
                view.picked_from = view.picked_to = 0;
            }
        }
        if (hidden[1]) {
            bool on = view.wrap;
            if (ImGui::MenuItem("Wrap", nullptr, &on)) {
                view.wrap = on;
            }
        }
        if (hidden[2] && ImGui::MenuItem("Copy")) {
            copy = true;
        }
        if (hidden[3]) {
            const char *disabled = app.project.open ? nullptr : "No project: exports go to its cache directory";
            if (ImGui::MenuItem("Export", nullptr, false, disabled == nullptr)) {
                exporting = true;
            }
            if (disabled != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", disabled);
            }
        }
        if (hidden[4] && ImGui::MenuItem("Clear View")) {
            clear_view();
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();
}

} // namespace rv_editor
