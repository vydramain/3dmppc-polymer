// Commits project changes once Review Changes has authorized them.

#include <algorithm>
#include <format>
#include <string>
#include <utility>

#include "pdk/rv_err.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_export.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

void rv_editor_shell_end_project_terminals(rv_editor_shell &shell)
{
    auto &app = shell.app;
    for (auto it = app.terminals.begin(); it != app.terminals.end();) {
        auto &[pane, view] = *it;
        if (view.cwd != app.project.root) {
            ++it;
            continue;
        }
        if (view.term != nullptr) {
            rv_editor_shell_stop_terminal(app, view);
        }
        if (shell.closing_terminal == pane) {
            shell.closing_terminal = rv_editor_tile_none;
        }
        it = app.terminals.erase(it);
    }
}

void rv_editor_shell_clear_project_state(rv_editor_shell &shell)
{
    auto &app = shell.app;
    if (app.project.open) {
        rv_editor_shell_end_project_terminals(shell);
    }
    app.build.clear_finished_result();
    app.build_first_seq = app.log.revision() + 1;
    app.exporter.clear_finished_result();
    rv_editor_pane_export_reset_form();
    app.session.clear_finished_session();
    app.session_first_seq = app.log.revision() + 1;
    app.session_profile.clear();
    app.candidate_session = 0;
    app.marks.clear();
    app.build_map.clear();
    app.build_alert = {};
    app.build_ended = {};
    app.problems.clear();
    app.open_requests.clear();
    app.requests.clear();
    app.code_tabs.clear();
    app.files_views.clear();
    app.open_as_text.clear();
    app.code_file.clear();
    app.files_select.reset();
    app.layout_request.reset();
    app.show_request = rv_editor_pane_kind::empty;
    app.new_file_request = false;
    app.open_folder_request = false;
    app.project_search = {};
    app.inputs_changed = true;
    app.run_after_build = false;
    app.run_after_stop = false;
    app.restart_after_build = false;
    app.restart_after_stop = false;
    app.unsaved_ask = rv_editor_unsaved_ask::none;
    app.texture_bake = {};
    app.assets_ui = {};
    app.scene_error.clear();
    app.scene_move_note.clear();
    rv_editor_app_reload_queue_drop(app);
    shell.closing = rv_editor_tile_none;
    shell.save_done = false;
    shell.save_as_buffer = 0;
    shell.save_as_error.clear();
    shell.revealed.clear();
    shell.open_browser = {};
    shell.ui_requests.clear();
    shell.ui.game_captured = false;
    shell.ui.files_selected.clear();
    shell.ui.scene_tabs = {};
}

const char *rv_editor_shell_project_operation(const rv_editor_shell &shell)
{
    const auto &app = shell.app;
    if (shell.saving) {
        return rv_editor_text("shell_save.waiting_for_nvim");
    }
    if (app.build.busy()) {
        return rv_editor_text("app_why_not.waiting_for_build");
    }
    if (app.exporter.busy() && !app.exporter.setup_job()) {
        return rv_editor_text("shell_save.waiting_for_export");
    }
    if (app.release.building) {
        return rv_editor_text("app_why_not.waiting_for_source");
    }
    if (app.session.live() || app.release.playing >= 0 || app.release.player != nullptr) {
        return rv_editor_text("shell_save.waiting_for_playback");
    }
    if (rv_editor_app_texture_bake_busy(app)) {
        return rv_editor_text("shell_save.waiting_for_texture_bake");
    }
    return nullptr;
}

void rv_editor_shell_project_switch_failed(rv_editor_shell &shell, const std::string &failure)
{
    shell.review.step = rv_editor_review_step::idle;
    if (!failure.empty()) {
        shell.review.error = rv_editor_text_format("shell_save.project_switch_failed", std::make_format_args(failure));
        shell.app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, shell.review.error);
    }
}

void rv_editor_shell_apply_project(rv_editor_shell &shell, rv_editor_project project)
{
    auto &app = shell.app;
    rv_editor_shell_clear_project_state(shell);
    rv_editor_app_open(app, std::move(project));
    const std::string project_name =
        app.project.disc_title.empty() ? app.project.root.filename().string() : app.project.disc_title;
    const auto title = rv_editor_text_format("shell_save.window_title", std::make_format_args(project_name));
    SDL_SetWindowTitle(shell.window, title.c_str());
}

} // namespace

void rv_editor_shell_open_project(rv_editor_shell &shell, const std::filesystem::path &path, bool discard)
{
    auto &app = shell.app;
    rv_editor_project project;
    std::string error;
    if (rv_editor_project_open(path, project, error) != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot open: " + error);
        rv_editor_shell_cancel_review(shell);
        return;
    }

    const auto root = project.root;
    const auto generation = shell.review.generation;
    const auto input_generation = app.nvim.input_generation();
    shell.review.step = rv_editor_review_step::opening;
    shell.review.error.clear();
    app.nvim.switch_root(root,
        discard,
        [&shell, project = std::move(project), discard, generation, input_generation](const std::string &failure) mutable {
            if (shell.review.generation != generation || shell.review.step != rv_editor_review_step::opening) {
                return;
            }
            auto &nvim = shell.app.nvim;
            const bool needs_review = !discard &&
                (nvim.buffer_state_lost() || rv_editor_app_scene_dirty(shell.app) ||
                    nvim.input_generation() != input_generation ||
                    std::ranges::any_of(nvim.buffers(), &rv_editor_nvim_buffer::modified));
            const char *blocked = rv_editor_shell_project_operation(shell);
            if (!failure.empty() || needs_review || blocked != nullptr) {
                const std::string reason = blocked != nullptr ? blocked : failure;
                // Queue the restore behind the directory change even if its reply timed out.
                nvim.restore_root([&shell, generation, reason](const std::string &why) {
                    if (shell.review.generation == generation) {
                        rv_editor_shell_project_switch_failed(shell, why.empty() ? reason : why);
                    }
                });
                return;
            }
            nvim.finish_switch_root(discard);
            rv_editor_shell_apply_project(shell, std::move(project));
            rv_editor_shell_cancel_review(shell);
        });
}

const char *rv_editor_shell_why_not_project_switch(const rv_editor_shell &shell)
{
    if (shell.app.nvim.switching_root() || shell.closing != rv_editor_tile_none) {
        return rv_editor_text("shell_save.waiting_for_nvim");
    }
    return rv_editor_shell_project_operation(shell);
}

void rv_editor_shell_request_close_project(rv_editor_shell &shell)
{
    if (shell.app.project.open) {
        rv_editor_shell_request_leave(shell, rv_editor_review_action::close_project);
    }
}

void rv_editor_shell_close_project(rv_editor_shell &shell)
{
    auto &app = shell.app;
    std::string error;
    if (app.nvim.begin_stop_project(error) != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "code editor: " + error);
    }
    (void)rv_editor_app_scene_close(app);
    error.clear();
    const int files_rc = app.files.close(error);
    if (files_rc != RV_OK && files_rc != RV_ERR_ALREADY) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "file watch: " + error);
    }
    rv_editor_shell_clear_project_state(shell);
    shell.start_selected = app.project.root;
    app.project = {};
    app.release = {};
    app.observe = {};
    app.findings = {};
    app.descriptions = {};
    // Cancel an inspection of the closed project and drop its findings.
    (void)app.inspect.request(app.project, {}, app.log, error);
    rv_editor_shell_page(shell, rv_editor_start_page::recent);
    SDL_SetWindowTitle(shell.window, "3dmppc-editor");
    rv_editor_shell_cancel_review(shell);
}

} // namespace rv_editor
