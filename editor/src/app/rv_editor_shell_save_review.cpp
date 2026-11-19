// Review Changes: the tab that lists what waits on unsaved files while quitting,
// opening, building or running, and the once-a-frame rule that shows and drops it.

#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "app/rv_editor_shell.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

using leave = rv_editor_shell::rv_editor_leave;

// Places, in text lines: a message, what did not save, the bullet list of Review Changes.
constexpr int message_lines = 2;
constexpr int review_failure_lines = 4;
constexpr int review_bullet_lines = 5;

} // namespace

void rv_editor_page_review(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    rv_editor_pane_header(rv_editor_text("shell_save.review_changes_header"), true, theme);
    if (shell.leaving == leave::none) {
        rv_editor_label(rv_editor_text("shell_save.nothing_waits"), rv_editor_label_kind::normal, theme);
        return;
    }
    rv_editor_state busy;
    if (shell.saving) {
        busy = rv_editor_state{ rv_editor_look::live, rv_editor_text("shell_save.waiting_for_nvim") };
    } else {
        busy = rv_editor_state{};
    }
    // Build, Run and Build and Restart go ahead without saving too: they use the files as on disk.
    const bool ahead = shell.leaving == leave::build || shell.leaving == leave::run || shell.leaving == leave::build_restart;
    const char *verb;
    if (shell.leaving == leave::build) {
        verb = rv_editor_text("shell_save.build_action");
    } else if (shell.leaving == leave::run) {
        verb = rv_editor_text("shell_save.run_action");
    } else {
        verb = rv_editor_text("shell_save.build_restart_action");
    }
    const char *message;
    if (shell.leaving == leave::quit) {
        message = rv_editor_text("shell_save.editor_closes");
    } else if (shell.leaving == leave::open) {
        message = rv_editor_text("shell_save.project_opens");
    } else {
        message = rv_editor_text("shell_save.build_saves");
    }
    rv_editor_message_line("##review_message", message, message_lines, 0.0f, theme);
    // The files that wait: a place of their own, the same size for any number of them.
    ImGui::BeginChild("##review_files", ImVec2(0.0f, static_cast<float>(review_bullet_lines) * ImGui::GetTextLineHeight()));
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
    ImGui::EndChild();
    rv_editor_shell_failures(shell, theme, review_failure_lines);
    if (shell.save_as_buffer != 0) {
        rv_editor_shell_save_as_body(shell, theme);
        return;
    }
    if (ahead) {
        const auto save_and = rv_editor_text_format("shell_save.save_and_action", std::make_format_args(verb));
        const bool save = rv_editor_button(save_and.c_str(), theme, busy);
        const auto action_saved = rv_editor_text_format("shell_save.action_saved_files", std::make_format_args(verb));
        rv_editor_flow(rv_editor_button_width(action_saved.c_str()));
        const bool saved = rv_editor_button(action_saved.c_str(), theme, busy);
        rv_editor_flow(rv_editor_button_width(rv_editor_text("shell_save.return_button")));
        const bool back = rv_editor_button(rv_editor_text("shell_save.return_button"), theme);
        if (save) {
            const std::vector<int64_t> ids = rv_editor_app_unsaved(app);
            if (rv_editor_shell_save_scene(shell) == RV_OK && ids.empty()) {
                shell.save_done = true;
            } else if (shell.save_failed.empty()) {
                rv_editor_shell_save(shell, ids);
            }
        }
        if (saved || back) {
            const leave was = shell.leaving;
            shell.leaving = leave::none;
            app.unsaved_open = rv_editor_unsaved_ask::none;
            shell.save_failed.clear();
            if (saved && was == leave::build) {
                rv_editor_app_build_saved(app);
            } else if (saved && was == leave::run) {
                rv_editor_app_run_saved(app);
            } else if (saved) {
                rv_editor_app_build_restart_saved(app);
            }
        }
        return;
    }
    const bool save = rv_editor_button(rv_editor_text("shell_save.save_all_button"), theme, busy);
    rv_editor_flow(rv_editor_button_width(rv_editor_text("shell_save.discard_all_button")));
    const bool discard = rv_editor_button(rv_editor_text("shell_save.discard_all_button"), theme, busy);
    rv_editor_flow(rv_editor_button_width(rv_editor_text("shell_save.return_button")));
    const bool back = rv_editor_button(rv_editor_text("shell_save.return_button"), theme);
    if (save && rv_editor_shell_save_scene(shell) == RV_OK) {
        rv_editor_shell_save(shell, {});
    }
    if (discard) {
        // The user's word: the changes go, then what waited goes ahead.
        app.nvim.discard(0);
        shell.save_failed.clear();
        shell.save_done = true;
    }
    if (back) {
        shell.leaving = leave::none;
        shell.app.unsaved_open = rv_editor_unsaved_ask::none;
        shell.save_failed.clear();
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
    if (alone && rv_editor_tile_find(shell.ws.layout, shell.save_as_pane) == rv_editor_tile_none) {
        shell.save_as_buffer = 0;
        shell.save_as_error.clear();
    }
}

} // namespace rv_editor
