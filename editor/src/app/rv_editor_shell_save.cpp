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
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

using leave = rv_editor_shell::rv_editor_leave;

// Save As field width in font size units.
constexpr float save_as_field_width_em = 40.0f;

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
    std::string project_name;
    if (app.project.disc_title.empty()) {
        project_name = app.project.root.filename().string();
    } else {
        project_name = app.project.disc_title;
    }
    const auto title = rv_editor_text_format("shell_save.window_title", std::make_format_args(project_name));
    SDL_SetWindowTitle(shell.window, title.c_str());
    app.open_requests.clear();
    shell.revealed.clear();
    rv_editor_log *log = &app.log;
    app.nvim.switch_root(app.project.root, [log](const std::string &failure) {
        if (!failure.empty()) {
            log->add(rv_editor_log_source::editor,
                rv_editor_log_level::error,
                "the code editor keeps the old project's files: " + failure);
        }
    });
}

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

// Save As under a question or on its own: the path field, Save and Cancel.
void rv_editor_shell_save_as_body(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    ImGui::TextWrapped("%s", rv_editor_text("shell_save.save_as_intro"));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * save_as_field_width_em);
    rv_editor_text_field("##save_as",
        shell.save_as_path,
        sizeof(shell.save_as_path),
        theme,
        { {}, false, false, shell.save_as_error.empty() ? nullptr : shell.save_as_error.c_str() });
    if (!shell.save_as_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s", shell.save_as_error.c_str());
        ImGui::PopStyleColor();
    }
    const rv_editor_state busy = [&]() {
        if (shell.saving) {
            return rv_editor_state{ rv_editor_look::live, rv_editor_text("shell_save.waiting_for_nvim") };
        }
        if (shell.save_as_path[0] == '\0') {
            return rv_editor_state{ rv_editor_look::live, rv_editor_text("shell_save.type_filename") };
        }
        return rv_editor_state{};
    }();
    const std::string save_label = std::string(rv_editor_text("shell_save.save_button")) + "##save_as";
    const bool save = rv_editor_button(save_label.c_str(), theme, busy);
    ImGui::SameLine();
    const std::string cancel_label = std::string(rv_editor_text("shell_save.cancel_button")) + "##save_as";
    if (rv_editor_button(cancel_label.c_str(), theme)) {
        shell.save_as_buffer = 0;
        shell.save_as_error.clear();
        return;
    }
    if (!save) {
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

// What did not save, and why; a Save As for each Untitled file among them.
void rv_editor_shell_failures(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    if (shell.saving) {
        ImGui::TextUnformatted(rv_editor_text("shell_save.saving"));
    }
    if (shell.save_failed.empty()) {
        return;
    }
    ImGui::TextUnformatted(rv_editor_text("shell_save.not_saved_header"));
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
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        const auto error_line = rv_editor_text_format("shell_save.not_saved_error", std::make_format_args(label, f.error));
        ImGui::TextWrapped("%s", error_line.c_str());
        ImGui::PopStyleColor();
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

// The scene first, as it saves at once; RV_OK on success, RV_ERR_* on failure.
int rv_editor_shell_save_scene(rv_editor_shell &shell)
{
    std::string error;
    const int code = rv_editor_app_scene_save(shell.app, error);
    if (code == RV_OK) {
        return RV_OK;
    }
    shell.save_failed.push_back({ 0, rv_editor_app_scene_name(shell.app), false, error });
    return code;
}

} // namespace

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
}

void rv_editor_shell_save_all(rv_editor_shell &shell)
{
    std::string error;
    if (rv_editor_app_scene_save(shell.app, error) != RV_OK) {
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
        if (it != shell.app.terminals.end() && it->second.term != nullptr && it->second.term->running()) {
            shell.closing_terminal = pane;
            return false;
        }
        return true;
    }
    // Closing Review Changes is Return: what waited does not happen.
    if (shell.ws.panes.panes[pane].kind == rv_editor_pane_kind::review_changes) {
        shell.leaving = rv_editor_shell::rv_editor_leave::none;
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

void rv_editor_shell_ask_close(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    rv_editor_state busy;
    if (shell.saving) {
        busy = rv_editor_state{ rv_editor_look::live, rv_editor_text("shell_save.waiting_for_nvim") };
    } else {
        busy = rv_editor_state{};
    }
    const int64_t win = app.nvim.window_for(shell.closing);
    const rv_editor_nvim_buffer *buf = app.nvim.buffer_in(win);
    const std::string name = buf == nullptr ? rv_editor_text("shell_save.this_file") : rv_editor_buffer_label(app, buf->name);
    rv_editor_ask_begin(rv_editor_text("shell_save.unsaved_changes_title"), theme);
    const auto msg = rv_editor_text_format("shell_save.has_unsaved_changes", std::make_format_args(name));
    ImGui::TextWrapped("%s", msg.c_str());
    rv_editor_shell_failures(shell, theme);
    if (shell.save_as_buffer != 0) {
        rv_editor_shell_save_as_body(shell, theme);
    } else {
        const bool save = rv_editor_button(rv_editor_text("shell_save.save_button"), theme, busy);
        ImGui::SameLine();
        const bool discard = rv_editor_button(rv_editor_text("shell_save.discard_button"), theme, busy);
        ImGui::SameLine();
        const bool keep = rv_editor_button(rv_editor_text("shell_save.keep_open_button"), theme);
        if (save && buf != nullptr) {
            rv_editor_shell_save(shell, { buf->id });
        }
        if (discard) {
            app.nvim.discard(win);
            (void)rv_editor_tile_remove(shell.ws.layout, shell.closing);
        }
        if (discard || keep) {
            shell.closing = rv_editor_tile_none;
            shell.save_failed.clear();
        }
    }
    rv_editor_ask_end();
}

void rv_editor_shell_ask_save_as(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_ask_begin(rv_editor_text("shell_save.save_as_title"), theme);
    rv_editor_shell_save_as_body(shell, theme);
    rv_editor_ask_end();
}

void rv_editor_page_review(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    rv_editor_pane_header(rv_editor_text("shell_save.review_changes_header"), true, theme);
    if (shell.leaving == leave::none) {
        ImGui::TextUnformatted(rv_editor_text("shell_save.nothing_waits"));
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
    ImGui::TextWrapped("%s", message);
    for (const rv_editor_nvim_buffer &b : app.nvim.modified()) {
        if (!ahead || !b.name.empty()) {
            ImGui::BulletText("%s", rv_editor_buffer_label(app, b.name).c_str());
        }
    }
    if (rv_editor_app_scene_dirty(app)) {
        const auto scene_name = rv_editor_app_scene_name(app);
        const auto scene_label = rv_editor_text_format("shell_save.scene_label", std::make_format_args(scene_name));
        ImGui::BulletText("%s", scene_label.c_str());
    }
    rv_editor_shell_failures(shell, theme);
    if (shell.save_as_buffer != 0) {
        rv_editor_shell_save_as_body(shell, theme);
        return;
    }
    if (ahead) {
        const auto save_and = rv_editor_text_format("shell_save.save_and_action", std::make_format_args(verb));
        const bool save = rv_editor_button(save_and.c_str(), theme, busy);
        ImGui::SameLine();
        const auto action_saved = rv_editor_text_format("shell_save.action_saved_files", std::make_format_args(verb));
        const bool saved = rv_editor_button(action_saved.c_str(), theme, busy);
        ImGui::SameLine();
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
    ImGui::SameLine();
    const bool discard = rv_editor_button(rv_editor_text("shell_save.discard_all_button"), theme, busy);
    ImGui::SameLine();
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
