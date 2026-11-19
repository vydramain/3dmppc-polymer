// What a code tile shows around its grid: the file tabs as the tile-tab row, the texts of a tab and the status line, and
// the swap-file ask.

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_panes.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_mode.hpp"

namespace rv_editor
{

// The whole path (relative inside `root`), the modified marker last: a tab and the status line never cut it.
rv_editor_code_text rv_editor_code_status_text(const std::string &name, const std::string &root, bool modified, float)
{
    const std::string marker = modified ? rv_editor_text("pane_code.file_modified_marker") : "";
    if (name.empty()) {
        return { rv_editor_text("pane_code.untitled_document") + marker, "" };
    }
    return { rv_editor_path_relative(name, root) + marker, name };
}

rv_editor_code_text rv_editor_code_tab_text(const std::string &name, const std::string &root, bool modified, float width)
{
    return rv_editor_code_status_text(name, root, modified, width);
}

bool rv_editor_code_tab_strip(const std::vector<rv_editor_code_tab_file> &files,
    const std::string &root,
    int *active,
    const rv_editor_theme &theme,
    rv_editor_tab_probe *probe)
{
    std::vector<std::string> text;
    std::vector<std::string> tips;
    for (const rv_editor_code_tab_file &f : files) {
        const rv_editor_code_text t = rv_editor_code_tab_text(f.name, root, f.modified, 0.0f);
        text.push_back(t.shown + "##" + f.name);
        tips.push_back(t.tooltip);
    }
    std::vector<const char *> labels;
    for (const std::string &t : text) {
        labels.push_back(t.c_str());
    }
    rv_editor_tab_probe own;
    rv_editor_tab_probe &seen = probe != nullptr ? *probe : own;
    const bool picked =
        rv_editor_tab_strip("##files", labels.data(), static_cast<int>(labels.size()), active, theme, {}, nullptr, 0.0f, &seen);
    // The full path of the tab under the pointer (inside the visible part of the strip).
    const ImVec2 mouse = ImGui::GetMousePos();
    for (size_t i = 0; i < seen.tabs.size() && i < tips.size(); ++i) {
        const auto &r = seen.tabs[i];
        if (!tips[i].empty() && mouse.x >= std::max(r.x0, seen.strip_x0) && mouse.x < std::min(r.x1, seen.strip_x1) &&
            mouse.y >= r.y0 && mouse.y < r.y1 && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            rv_editor_tooltip(tips[i].c_str());
        }
    }
    return picked;
}

// A card under the tile's row, the size of the grid's place; the button tooltips are lines of its text.
rv_editor_swap_action rv_editor_code_swap_place(rv_editor_swap_view view, long long pid, const rv_editor_theme &theme)
{
    rv_editor_swap_action action;
    if (view == rv_editor_swap_view::none) {
        return action;
    }
    rv_editor_mode_card_spec spec;
    spec.title = rv_editor_text("pane_code.swap_title");
    if (view == rv_editor_swap_view::recoverable) {
        spec.text = std::string(rv_editor_text("pane_code.swap_recovery_message")) + "\n\n" +
            rv_editor_text("pane_code.swap_recover_button") + ": " + rv_editor_text("pane_code.swap_recover_button_tooltip") +
            "\n" + rv_editor_text("pane_code.swap_discard_button") + ": " +
            rv_editor_text("pane_code.swap_discard_button_tooltip");
        spec.buttons = { rv_editor_text("pane_code.swap_recover_button"), rv_editor_text("pane_code.swap_discard_button") };
    } else {
        spec.text = rv_editor_text_format("pane_code.readonly_process_editing", std::make_format_args(pid));
    }
    const int pressed = rv_editor_mode_card_begin("##swap", spec, theme);
    rv_editor_mode_card_end();
    action.recover = pressed == 0;
    action.discard = pressed == 1;
    return action;
}

} // namespace rv_editor
