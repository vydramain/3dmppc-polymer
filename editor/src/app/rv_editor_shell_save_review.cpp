// Review Changes: the tab that lists what waits on unsaved files while quitting,
// opening, building or running, and the once-a-frame rule that shows and drops it.

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

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
using step = rv_editor_review_step;

bool rv_editor_review_switches_project(leave action)
{
    return action == leave::open || action == leave::close_project;
}

bool rv_editor_has_buffer_state(const rv_editor_nvim &nvim)
{
    return nvim.running() || nvim.buffer_state_lost() || std::ranges::any_of(nvim.buffers(), &rv_editor_nvim_buffer::modified);
}

void rv_editor_review_check_failed(rv_editor_shell &shell, const std::string &failure)
{
    shell.review.error = rv_editor_text_format("shell_save.check_failed", std::make_format_args(failure));
    shell.app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, shell.review.error);
}

void rv_editor_review_check(rv_editor_shell &shell)
{
    auto &nvim = shell.app.nvim;
    const auto input_generation = nvim.input_generation();
    if (nvim.running() && !input_generation) {
        return;
    }
    shell.review.input_generation = input_generation.value_or(0);
    const auto generation = shell.review.generation;
    shell.review.step = step::checking;
    auto done = [&shell, generation](bool dirty, const std::string &failure) {
        if (shell.review.generation != generation || shell.review.step != step::checking) {
            return;
        }
        shell.review.step = !dirty && failure.empty() ? step::authorized : step::idle;
        if (!failure.empty()) {
            rv_editor_review_check_failed(shell, failure);
        }
    };
    if (rv_editor_has_buffer_state(nvim)) {
        nvim.check_modified(std::move(done));
    } else {
        done(false, {});
    }
}

void rv_editor_review_build_saved(rv_editor_app &app, leave action)
{
    if (action == leave::build) {
        rv_editor_app_build_saved(app);
    } else if (action == leave::run) {
        rv_editor_app_run_saved(app);
    } else {
        rv_editor_app_build_restart_saved(app);
    }
}

void rv_editor_leave_project(rv_editor_shell &shell, leave action, const std::filesystem::path &target, bool discard)
{
    if (action == leave::quit) {
        if (discard) {
            shell.app.nvim.discard(0);
        }
        shell.quit_now = true;
        rv_editor_shell_cancel_review(shell);
    } else if (action == leave::open) {
        rv_editor_shell_open_project(shell, target, discard);
    } else if (action == leave::close_project) {
        rv_editor_shell_close_project(shell);
    }
}

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
    const rv_editor_app &app = shell.app;
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
    if (!shell.review.error.empty()) {
        rv_editor_label(shell.review.error.c_str(), rv_editor_label_kind::error, theme, rv_editor_label_layout::wrap);
    } else if (shell.review.step == step::check || shell.review.step == step::checking) {
        rv_editor_label(rv_editor_text("shell_save.checking_changes"),
            rv_editor_label_kind::dim,
            theme,
            rv_editor_label_layout::line);
    }
    rv_editor_shell_failures(shell, theme);
}

} // namespace

bool rv_editor_review_leaves_project(leave action)
{
    return action == leave::quit || rv_editor_review_switches_project(action);
}

void rv_editor_shell_cancel_review(rv_editor_shell &shell)
{
    const auto generation = shell.review.generation + 1;
    shell.review = {};
    shell.review.generation = generation;
    shell.leaving = leave::none;
    shell.app.unsaved_open = rv_editor_unsaved_ask::none;
    shell.save_failed.clear();
}

void rv_editor_shell_begin_review(rv_editor_shell &shell, leave action)
{
    rv_editor_shell_cancel_review(shell);
    shell.leaving = action;
}

void rv_editor_shell_request_leave(rv_editor_shell &shell, leave action, std::filesystem::path target)
{
    if (!rv_editor_review_leaves_project(action)) {
        return;
    }
    if (rv_editor_review_switches_project(action)) {
        if (const char *reason = rv_editor_shell_why_not_project_switch(shell)) {
            shell.app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, reason);
            return;
        }
    } else if (shell.leaving != leave::none) {
        return;
    }
    if (!shell.saving && !rv_editor_has_buffer_state(shell.app.nvim) && !rv_editor_app_scene_dirty(shell.app)) {
        rv_editor_leave_project(shell, action, target, false);
        return;
    }
    rv_editor_shell_begin_review(shell, action);
    shell.review.target = std::move(target);
    shell.review.step = step::check;
}

void rv_editor_shell_request_open(rv_editor_shell &shell, const std::filesystem::path &path)
{
    rv_editor_shell_request_leave(shell, leave::open, path);
}

bool rv_editor_shell_may_quit(rv_editor_shell &shell)
{
    if (!shell.quit_now) {
        rv_editor_shell_request_leave(shell, leave::quit);
    }
    return shell.quit_now;
}

void rv_editor_shell_after_save(rv_editor_shell &shell)
{
    if (std::exchange(shell.save_done, false) && shell.closing != rv_editor_tile_none) {
        (void)rv_editor_tile_remove(shell.ws.layout, shell.closing);
        shell.closing = rv_editor_tile_none;
    }
    if (shell.saving ||
        (rv_editor_review_switches_project(shell.leaving) &&
            (shell.app.nvim.switching_root() || shell.closing != rv_editor_tile_none))) {
        return;
    }
    if (shell.review.step == step::check) {
        rv_editor_review_check(shell);
    }
    const bool discard = shell.review.step == step::discard;
    if (!discard && shell.review.step != step::authorized) {
        return;
    }
    // Leave Return available through the mouse-up frame when its press was drawn.
    if (ImGui::GetCurrentContext() != nullptr && shell.review.return_active_frame == ImGui::GetFrameCount()) {
        return;
    }
    auto &app = shell.app;
    const auto action = shell.leaving;
    if (!rv_editor_review_leaves_project(action)) {
        // Just written: the watcher may not have said so yet.
        app.inputs_changed = true;
        rv_editor_shell_cancel_review(shell);
        rv_editor_review_build_saved(app, action);
        return;
    }
    if (!discard && app.nvim.buffer_state_lost()) {
        shell.review.step = step::idle;
        rv_editor_review_check_failed(shell, rv_editor_text("nvim_rpc.not_running"));
        return;
    }
    if (!discard && app.nvim.running() && app.nvim.input_generation() != shell.review.input_generation) {
        // Key or mouse input submitted after this snapshot needs a fresh check.
        shell.review.step = step::check;
        return;
    }
    if (!discard &&
        (rv_editor_app_scene_dirty(app) || std::ranges::any_of(app.nvim.buffers(), &rv_editor_nvim_buffer::modified))) {
        shell.review.step = step::idle;
        return;
    }

    if (rv_editor_review_switches_project(action)) {
        if (const char *reason = rv_editor_shell_why_not_finish_project_switch(shell)) {
            shell.review.step = step::idle;
            shell.review.error = reason;
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, reason);
            return;
        }
    }
    rv_editor_leave_project(shell, action, shell.review.target, discard);
}

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
    const bool opening = shell.review.step == step::opening;
    const rv_editor_state busy = shell.saving || opening || app.nvim.switching_root() ?
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
            if (rv_editor_button(names[i].c_str(), theme, i == 2 && !opening ? rv_editor_state{} : busy)) {
                pressed = static_cast<int>(i);
            }
            if (i == 2 && ImGui::IsItemActive()) {
                shell.review.return_active_frame = ImGui::GetFrameCount();
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
    } else if (what == leave::close_project) {
        message = rv_editor_text("shell_save.project_closes");
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
        rv_editor_shell_cancel_review(shell);
    } else if (pressed == 0) {
        rv_editor_shell_review_save(shell);
    } else if (ahead) {
        rv_editor_shell_cancel_review(shell);
        rv_editor_review_build_saved(app, what);
    } else {
        // Only an explicit Discard can bypass the verification step.
        shell.review.step = step::discard;
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
