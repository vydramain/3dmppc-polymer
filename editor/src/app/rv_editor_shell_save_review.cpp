// Review Changes: the tab that lists what waits on unsaved files while quitting,
// opening, building or running, and the once-a-frame rule that shows and drops it.

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

using leave = rv_editor_shell::rv_editor_leave;

// Build, Run and Build and Restart go ahead without saving too: they use the files as on disk.
bool rv_editor_review_ahead(leave what)
{
    return what == leave::build || what == leave::run || what == leave::build_restart;
}

const char *rv_editor_review_verb(leave what)
{
    if (what == leave::build) {
        return rv_editor_text("shell_save.build_action");
    }
    return what == leave::run ? rv_editor_text("shell_save.run_action") : rv_editor_text("shell_save.build_restart_action");
}

float rv_editor_review_row_width(const std::vector<std::string> &labels)
{
    float width = ImGui::GetStyle().ItemSpacing.x * static_cast<float>(labels.size() - 1);
    for (const std::string &l : labels) {
        width += rv_editor_button_width(l.c_str());
    }
    return width;
}

// The buttons of the shelf for what waits, left to right: the first, the second, Return.
std::vector<std::string> rv_editor_review_buttons(leave what)
{
    if (!rv_editor_review_ahead(what)) {
        return { rv_editor_text("shell_save.save_all_button"),
            rv_editor_text("shell_save.discard_all_button"),
            rv_editor_text("shell_save.return_button") };
    }
    const char *verb = rv_editor_review_verb(what);
    return { rv_editor_text_format("shell_save.save_and_action", std::make_format_args(verb)),
        rv_editor_text_format("shell_save.action_saved_files", std::make_format_args(verb)),
        rv_editor_text("shell_save.return_button") };
}

// The files that wait, then what did not save, in the content area.
void rv_editor_review_list(rv_editor_shell &shell, const rv_editor_theme &theme, bool ahead)
{
    rv_editor_app &app = shell.app;
    for (const rv_editor_nvim_buffer &b : app.nvim.modified()) {
        if (!ahead || !b.name.empty()) {
            const std::string label = rv_editor_shell_buffer_label(app, b.name);
            rv_editor_label(label.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::bullet);
        }
    }
    if (rv_editor_app_scene_dirty(app)) {
        const auto scene_name = rv_editor_app_scene_name(app);
        const auto scene_label = rv_editor_text_format("shell_save.scene_label", std::make_format_args(scene_name));
        rv_editor_label(scene_label.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::bullet);
    }
    rv_editor_shell_failures(shell, theme);
}

} // namespace

float rv_editor_review_content_width()
{
    float widest = rv_editor_review_row_width(rv_editor_review_buttons(leave::quit));
    for (const leave what : { leave::build, leave::run, leave::build_restart }) {
        widest = std::max(widest, rv_editor_review_row_width(rv_editor_review_buttons(what)));
    }
    return std::max(rv_editor_tiled_shelf_width(widest), rv_editor_message_strip_min_width({}));
}

void rv_editor_page_review(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    const leave what = shell.leaving;
    if (what != leave::none && shell.save_as_buffer != 0) {
        rv_editor_shell_save_as_card(shell, theme);
        return;
    }
    const bool ahead = rv_editor_review_ahead(what);
    const rv_editor_state busy = shell.saving ?
        rv_editor_state{ rv_editor_look::live, rv_editor_text("shell_save.waiting_for_nvim") } :
        rv_editor_state{};
    // The shelf keeps its row with nothing waiting.
    rv_editor_shelf_begin("##review_shelf", theme);
    int pressed = -1;
    if (what == leave::none) {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    } else {
        const std::vector<std::string> names = rv_editor_review_buttons(what);
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                ImGui::SameLine();
            }
            if (rv_editor_button(names[i].c_str(), theme, i == 2 ? rv_editor_state{} : busy)) {
                pressed = static_cast<int>(i);
            }
        }
    }
    rv_editor_shelf_end();
    // What happens on leaving is a message; with nothing waiting the area's one dim line says so.
    std::string message;
    if (what == leave::quit) {
        message = rv_editor_text("shell_save.editor_closes");
    } else if (what == leave::open) {
        message = rv_editor_text("shell_save.project_opens");
    } else if (ahead) {
        message = rv_editor_text("shell_save.build_saves");
    }
    const float width = rv_editor_review_content_width();
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, width);
    (void)rv_editor_tiled_area_begin("##review_area",
        width,
        theme,
        {},
        false,
        0.0f,
        rv_editor_message_strip_height(message, {}, strip_w));
    if (what != leave::none) {
        rv_editor_review_list(shell, theme, ahead);
    } else {
        rv_editor_content_empty_row(rv_editor_text("shell_save.nothing_waits"), theme);
    }
    rv_editor_tiled_area_end(theme);
    (void)rv_editor_message_strip("##review_message", message, rv_editor_label_kind::normal, {}, theme, strip_w);
    if (pressed < 0) {
        return;
    }
    if (pressed == 2) {
        shell.leaving = leave::none;
        app.unsaved_open = rv_editor_unsaved_ask::none;
        shell.save_failed.clear();
        return;
    }
    if (pressed == 0) {
        shell.save_failed.clear();
        if (rv_editor_shell_save_scene(shell) != RV_OK) {
            return;
        }
        const std::vector<int64_t> ids = ahead ? rv_editor_app_unsaved(app) : std::vector<int64_t>{};
        if (ahead && ids.empty()) {
            shell.save_done = true;
        } else {
            rv_editor_shell_save(shell, ids);
        }
        return;
    }
    if (ahead) {
        shell.leaving = leave::none;
        app.unsaved_open = rv_editor_unsaved_ask::none;
        shell.save_failed.clear();
        if (what == leave::build) {
            rv_editor_app_build_saved(app);
        } else if (what == leave::run) {
            rv_editor_app_run_saved(app);
        } else {
            rv_editor_app_build_restart_saved(app);
        }
        return;
    }
    if (pressed == 1) {
        // The user's word: the changes go, then what waited goes ahead.
        app.nvim.discard(0);
        shell.save_failed.clear();
        shell.save_done = true;
    }
}

void rv_editor_shell_review_tab(rv_editor_shell &shell)
{
    rv_editor_pane_id shown = rv_editor_tile_none;
    for (const rv_editor_tile_node &node : shell.ws.layout.nodes) {
        if (node.kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        for (const rv_editor_pane_id pane : node.leaf.tabs) {
            if (shell.ws.panes.panes[pane].kind == rv_editor_pane_kind::review_changes) {
                shown = pane;
            }
        }
    }
    const bool waits = shell.leaving != leave::none;
    if (waits && shown == rv_editor_tile_none && shell.app.project.open) {
        rv_editor_shell_show_pane(shell, rv_editor_pane_kind::review_changes, true);
    }
    if (!waits && shown != rv_editor_tile_none) {
        (void)rv_editor_tile_remove(shell.ws.layout, shown);
    }
    const bool alone = shell.save_as_buffer != 0 && shell.closing == rv_editor_tile_none && !waits;
    const rv_editor_pane_id pane = shell.save_as_pane;
    const bool kept = pane < shell.ws.panes.panes.size() && shell.ws.panes.panes[pane].kind == rv_editor_pane_kind::code &&
        rv_editor_tile_find(shell.ws.layout, pane) != rv_editor_tile_none;
    if (alone && !kept) {
        shell.save_as_buffer = 0;
        shell.save_as_error.clear();
    }
}

} // namespace rv_editor
