// Unsaved buffers: the questions before a code tile closes, the window closes or
// another project opens, and Save As, each inside the tile or tab it is about.
// Nothing is dropped unless the user says Discard, and nothing counts as saved
// until nvim says the write happened.

#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "app/rv_editor_shell.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_mode.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

using leave = rv_editor_shell::rv_editor_leave;

// A buffer's file as the project sees it: relative to the root when inside it.
std::string rv_editor_buffer_label(const rv_editor_app &app, const std::string &name)
{
    if (name.empty()) {
        return rv_editor_text("shell_save.untitled");
    }
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(name, app.project.root, ec);
    const bool inside = app.project.open && !ec && !rel.empty() && *rel.begin() != "..";
    return inside ? rel.string() : name;
}

// Opens `path` now: nvim's buffers of the old project go (none is modified by
// now) and it moves to the new root.
void rv_editor_shell_open_now(rv_editor_shell &shell, const std::filesystem::path &path)
{
    rv_editor_app &app = shell.app;
    if (rv_editor_app_open(app, path) != RV_OK) {
        return;
    }
    const std::string project_name =
        app.project.disc_title.empty() ? app.project.root.filename().string() : app.project.disc_title;
    const auto title = rv_editor_text_format("shell_save.window_title", std::make_format_args(project_name));
    SDL_SetWindowTitle(shell.window, title.c_str());
    app.open_requests.clear();
    shell.revealed.clear();
    // Another project takes the keyboard back from the game.
    shell.ui.game_captured = false;
    // The old project's entry is not in the new tree.
    shell.ui.files_selected.clear();
    shell.ui.scene_tabs = {};
    rv_editor_log *log = &app.log;
    app.nvim.switch_root(app.project.root, [log](const std::string &failure) {
        if (!failure.empty()) {
            log->add(rv_editor_log_source::editor,
                rv_editor_log_level::error,
                "the code editor keeps the old project's files: " + failure);
        }
    });
}

} // namespace

// Saves `ids` (every modified buffer when empty). The outcome lands in
// shell.save_failed, or shell.save_done when every file was written.
void rv_editor_shell_save(rv_editor_shell &shell, const std::vector<int64_t> &ids)
{
    // No code editor running, no buffers of its to write: done (a scene may have been saved).
    if (!shell.app.nvim.running()) {
        shell.save_failed.clear();
        shell.save_done = true;
        return;
    }
    shell.saving = true;
    shell.save_failed.clear();
    shell.save_done = false;
    rv_editor_shell *s = &shell;
    shell.app.nvim.save(ids, [s](const std::vector<rv_editor_nvim_saved> &saved, const std::string &failure) {
        s->saving = false;
        for (const rv_editor_nvim_saved &f : saved) {
            const std::string label = rv_editor_buffer_label(s->app, f.name);
            if (f.ok) {
                s->app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "saved " + label);
                continue;
            }
            s->save_failed.push_back(f);
            s->app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "not saved: " + label + ": " + f.error);
        }
        if (!failure.empty()) {
            s->save_failed.push_back({ 0, {}, false, failure });
            s->app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "nothing saved: " + failure);
        }
        s->save_done = s->save_failed.empty();
    });
}

// Save As under a question or on its own: a card whose text area holds the path field and its error, with Save and
// Cancel under it.
void rv_editor_shell_save_as_card(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    rv_editor_mode_card_spec spec;
    spec.title = rv_editor_text("shell_save.save_as_title");
    spec.buttons = { rv_editor_text("shell_save.save_button"), rv_editor_text("shell_save.cancel_button") };
    spec.message_kind = rv_editor_label_kind::dim;
    if (shell.saving) {
        spec.message = rv_editor_text("shell_save.waiting_for_nvim");
    } else if (shell.save_as_path[0] == '\0') {
        spec.message = rv_editor_text("shell_save.type_filename");
    }
    spec.body = [&shell, &app, &theme] {
        rv_editor_label(rv_editor_text("shell_save.save_as_intro"),
            rv_editor_label_kind::normal,
            theme,
            rv_editor_label_layout::wrap);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        rv_editor_path_field("##save_as",
            shell.save_as_path,
            sizeof(shell.save_as_path),
            app.project.root.string(),
            theme,
            { {}, false, false, shell.save_as_error.empty() ? nullptr : shell.save_as_error.c_str() });
        if (!shell.save_as_error.empty()) {
            rv_editor_label(shell.save_as_error.c_str(), rv_editor_label_kind::error, theme, rv_editor_label_layout::wrap);
        }
    };
    const int pressed = rv_editor_mode_card_begin("##save_as_card", spec, theme);
    rv_editor_mode_card_end();
    if (pressed == 1) {
        shell.save_as_buffer = 0;
        shell.save_as_error.clear();
        return;
    }
    if (pressed != 0 || shell.saving || shell.save_as_path[0] == '\0') {
        return;
    }
    std::filesystem::path path(shell.save_as_path);
    if (path.is_relative() && app.project.open) {
        path = app.project.root / path;
    }
    shell.saving = true;
    shell.save_as_error.clear();
    rv_editor_shell *s = &shell;
    const int64_t id = shell.save_as_buffer;
    app.nvim.save_as(id, path, [s, id](const std::vector<rv_editor_nvim_saved> &saved, const std::string &failure) {
        s->saving = false;
        if (failure.empty() && !saved.empty() && saved.front().ok) {
            s->app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "saved " + saved.front().name);
            std::erase_if(s->save_failed, [id](const rv_editor_nvim_saved &f) {
                return f.id == id;
            });
            s->save_as_buffer = 0;
            return;
        }
        if (!failure.empty()) {
            s->save_as_error = failure;
        } else if (saved.empty()) {
            s->save_as_error = rv_editor_text("shell_save.nvim_not_said");
        } else {
            s->save_as_error = saved.front().error;
        }
    });
}

// What did not save, and why, as lines of the area it is drawn in; a Save As for each Untitled file among them.
void rv_editor_shell_failures(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    if (shell.saving) {
        rv_editor_label(rv_editor_text("shell_save.saving"), rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
    } else if (!shell.save_failed.empty()) {
        rv_editor_label(rv_editor_text("shell_save.not_saved_header"),
            rv_editor_label_kind::normal,
            theme,
            rv_editor_label_layout::wrap);
    }
    for (size_t i = 0; i < shell.save_failed.size(); ++i) {
        const rv_editor_nvim_saved &f = shell.save_failed[i];
        std::string label;
        if (f.id != 0) {
            label = rv_editor_buffer_label(shell.app, f.name);
        } else if (f.name.empty()) {
            label = rv_editor_text("shell_save.nvim_name");
        } else {
            label = f.name;
        }
        const auto error_line = rv_editor_text_format("shell_save.not_saved_error", std::make_format_args(label, f.error));
        rv_editor_label(error_line.c_str(), rv_editor_label_kind::error, theme, rv_editor_label_layout::wrap);
        if (f.id != 0 && f.name.empty() && shell.save_as_buffer == 0) {
            ImGui::PushID(static_cast<int>(i));
            if (rv_editor_button(rv_editor_text("shell_save.save_as_button"), theme)) {
                shell.save_as_buffer = f.id;
                shell.save_as_path[0] = '\0';
                shell.save_as_error.clear();
            }
            ImGui::PopID();
        }
    }
}

namespace
{

// ALREADY: the scene is on disk already, so it is no failure.
bool rv_editor_scene_save_failed(int code)
{
    return code != RV_OK && code != RV_ERR_ALREADY;
}

} // namespace

// The scene first, as it saves at once; RV_OK on success or when already saved, RV_ERR_* on failure.
int rv_editor_shell_save_scene(rv_editor_shell &shell)
{
    std::string error;
    const int code = rv_editor_app_scene_save(shell.app, error);
    if (rv_editor_scene_save_failed(code)) {
        shell.save_failed.push_back({ 0, rv_editor_app_scene_name(shell.app), false, error });
        return code;
    }
    return RV_OK;
}

std::string rv_editor_shell_buffer_label(const rv_editor_app &app, const std::string &name)
{
    return rv_editor_buffer_label(app, name);
}

void rv_editor_shell_request_open(rv_editor_shell &shell, const std::filesystem::path &path)
{
    if ((shell.app.nvim.running() && !shell.app.nvim.modified().empty()) || rv_editor_app_scene_dirty(shell.app)) {
        shell.leaving = leave::open;
        shell.leaving_to = path;
        return;
    }
    rv_editor_shell_open_now(shell, path);
}

void rv_editor_shell_after_save(rv_editor_shell &shell)
{
    if (!shell.save_done) {
        return;
    }
    shell.save_done = false;
    if (shell.closing != rv_editor_tile_none) {
        (void)rv_editor_tile_remove(shell.ws.layout, shell.closing);
        shell.closing = rv_editor_tile_none;
        return;
    }
    if (shell.leaving == leave::quit) {
        shell.quit_now = true;
    } else if (shell.leaving == leave::open) {
        rv_editor_shell_open_now(shell, shell.leaving_to);
    } else if (shell.leaving == leave::build || shell.leaving == leave::run || shell.leaving == leave::build_restart) {
        // Just written: the watcher may not have said so yet.
        shell.app.inputs_changed = true;
        if (shell.leaving == leave::build) {
            rv_editor_app_build_saved(shell.app);
        } else if (shell.leaving == leave::run) {
            rv_editor_app_run_saved(shell.app);
        } else {
            rv_editor_app_build_restart_saved(shell.app);
        }
    }
    shell.leaving = leave::none;
    shell.app.unsaved_open = rv_editor_unsaved_ask::none;
}

void rv_editor_shell_save_all(rv_editor_shell &shell)
{
    std::string error;
    const int code = rv_editor_app_scene_save(shell.app, error);
    if (rv_editor_scene_save_failed(code)) {
        shell.app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not saved: " + error);
    }
    rv_editor_shell_save(shell, {});
}

void rv_editor_shell_save_as_start(rv_editor_shell &shell)
{
    const rv_editor_workspace &ws = shell.ws;
    if (ws.focused_leaf >= ws.layout.nodes.size() || ws.layout.nodes[ws.focused_leaf].kind != rv_editor_tile_kind::leaf) {
        return;
    }
    const rv_editor_tile_leaf &leaf = ws.layout.nodes[ws.focused_leaf].leaf;
    if (leaf.tabs.empty() || ws.panes.panes[leaf.tabs[leaf.active]].kind != rv_editor_pane_kind::code) {
        return;
    }
    const rv_editor_nvim_buffer *buf = shell.app.nvim.buffer_in(shell.app.nvim.window_for(leaf.tabs[leaf.active]));
    if (buf == nullptr) {
        return;
    }
    shell.save_as_buffer = buf->id;
    shell.save_as_pane = leaf.tabs[leaf.active];
    const std::string label = buf->name.empty() ? std::string() : rv_editor_buffer_label(shell.app, buf->name);
    std::snprintf(shell.save_as_path, sizeof(shell.save_as_path), "%s", label.c_str());
    shell.save_as_error.clear();
}

bool rv_editor_shell_close_pane(void *context, rv_editor_pane_id pane)
{
    rv_editor_shell &shell = *static_cast<rv_editor_shell *>(context);
    if (shell.ws.panes.panes[pane].kind == rv_editor_pane_kind::terminal) {
        const auto it = shell.app.terminals.find(pane);
        if (it != shell.app.terminals.end() && it->second.end_answer == rv_editor_terminal_end_answer::end) {
            it->second.end_answer = rv_editor_terminal_end_answer::none;
            shell.closing_terminal = rv_editor_tile_none;
            return true;
        }
        if (it != shell.app.terminals.end() && it->second.term != nullptr && it->second.term->running()) {
            shell.closing_terminal = pane;
            return false;
        }
        return true;
    }
    // Closing Review Changes is Return: what waited does not happen.
    if (shell.ws.panes.panes[pane].kind == rv_editor_pane_kind::review_changes) {
        shell.leaving = rv_editor_shell::rv_editor_leave::none;
        shell.app.unsaved_open = rv_editor_unsaved_ask::none;
        shell.save_failed.clear();
        return true;
    }
    if (shell.ws.panes.panes[pane].kind != rv_editor_pane_kind::code || !shell.app.nvim.running()) {
        return true;
    }
    const int64_t win = shell.app.nvim.window_for(pane);
    if (win == 0 || !shell.app.nvim.modified_only_in(win)) {
        return true;
    }
    shell.closing = pane;
    return false;
}

bool rv_editor_shell_may_quit(rv_editor_shell &shell)
{
    const bool code = shell.app.nvim.running() && !shell.app.nvim.modified().empty();
    if (shell.quit_now || (!code && !rv_editor_app_scene_dirty(shell.app))) {
        return true;
    }
    shell.leaving = leave::quit;
    return false;
}

// The Unsaved Changes ask: a card over the tile, the failures in its text area.
void rv_editor_shell_ask_close(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    if (shell.save_as_buffer != 0) {
        rv_editor_shell_save_as_card(shell, theme);
        return;
    }
    rv_editor_app &app = shell.app;
    const int64_t win = app.nvim.window_for(shell.closing);
    const rv_editor_nvim_buffer *buf = app.nvim.buffer_in(win);
    const std::string name = buf == nullptr ? rv_editor_text("shell_save.this_file") : rv_editor_buffer_label(app, buf->name);
    const auto msg = rv_editor_text_format("shell_save.has_unsaved_changes", std::make_format_args(name));
    rv_editor_mode_card_spec spec;
    spec.title = rv_editor_text("shell_save.unsaved_changes_title");
    spec.buttons = { rv_editor_text("shell_save.save_button"),
        rv_editor_text("shell_save.discard_button"),
        rv_editor_text("shell_save.keep_open_button") };
    spec.message_kind = rv_editor_label_kind::dim;
    spec.message = shell.saving ? rv_editor_text("shell_save.waiting_for_nvim") : "";
    spec.body = [&shell, &theme, &msg] {
        rv_editor_label(msg.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
        // The place of the Saving / Not saved line is kept, so the text area has one content height whatever the state.
        if (!shell.saving && shell.save_failed.empty()) {
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
        }
        rv_editor_shell_failures(shell, theme);
    };
    const int pressed = rv_editor_mode_card_begin("##unsaved_card", spec, theme);
    rv_editor_mode_card_end();
    // A save under way answers no other Save or Discard.
    if (pressed == 0 && !shell.saving && buf != nullptr) {
        rv_editor_shell_save(shell, { buf->id });
    }
    if (pressed == 1 && !shell.saving) {
        app.nvim.discard(win);
        (void)rv_editor_tile_remove(shell.ws.layout, shell.closing);
    }
    if ((pressed == 1 && !shell.saving) || pressed == 2) {
        shell.closing = rv_editor_tile_none;
        shell.save_failed.clear();
    }
}

void rv_editor_shell_ask_save_as(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_shell_save_as_card(shell, theme);
}

bool rv_editor_shell_code_asks(rv_editor_shell &shell, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    if (pane == shell.closing) {
        rv_editor_shell_ask_close(shell, theme);
        return true;
    }
    if (pane == shell.save_as_pane && shell.save_as_buffer != 0 && shell.leaving == rv_editor_shell::rv_editor_leave::none) {
        rv_editor_shell_ask_save_as(shell, theme);
        return true;
    }
    return false;
}

} // namespace rv_editor
