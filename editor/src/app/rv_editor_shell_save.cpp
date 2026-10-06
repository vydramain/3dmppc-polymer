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
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

using leave = rv_editor_shell::rv_editor_leave;

// A buffer's file as the project sees it: relative to the root when inside it.
std::string rv_editor_buffer_label(const rv_editor_app &app, const std::string &name)
{
    if (name.empty()) {
        return "Untitled";
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
    if (!rv_editor_app_open(app, path)) {
        return;
    }
    const std::string title = "3dmppc-editor - " +
        (app.project.disc_title.empty() ? app.project.root.filename().string() : app.project.disc_title);
    SDL_SetWindowTitle(shell.window, title.c_str());
    app.open_requests.clear();
    shell.revealed.clear();
    rv_editor_log *log = &app.log;
    app.nvim.switch_root(app.project.root, [log](const std::string &failure) {
        if (!failure.empty()) {
            log->add(rv_editor_log_source::editor, rv_editor_log_level::error,
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
            s->app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
                "not saved: " + label + ": " + f.error);
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
    ImGui::TextWrapped("Save as, relative to the project; an existing file is never replaced:");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 40.0f);
    rv_editor_text_field("##save_as", shell.save_as_path, sizeof(shell.save_as_path), theme,
        { {}, false, false, shell.save_as_error.empty() ? nullptr : shell.save_as_error.c_str() });
    if (!shell.save_as_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s", shell.save_as_error.c_str());
        ImGui::PopStyleColor();
    }
    const rv_editor_state busy = shell.saving ? rv_editor_state{ rv_editor_look::live, "Waiting for nvim to answer" }
        : shell.save_as_path[0] == '\0'      ? rv_editor_state{ rv_editor_look::live, "Type a file name first" }
                                             : rv_editor_state{};
    const bool save = rv_editor_button("Save##save_as", theme, busy);
    ImGui::SameLine();
    if (rv_editor_button("Cancel##save_as", theme)) {
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
            std::erase_if(s->save_failed, [id](const rv_editor_nvim_saved &f) { return f.id == id; });
            s->save_as_buffer = 0;
            return;
        }
        s->save_as_error = !failure.empty() ? failure : saved.empty() ? "nvim did not say" : saved.front().error;
    });
}

// What did not save, and why; a Save As for each Untitled file among them.
void rv_editor_shell_failures(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    if (shell.saving) {
        ImGui::TextUnformatted("Saving...");
    }
    if (shell.save_failed.empty()) {
        return;
    }
    ImGui::TextUnformatted("Not saved, so nothing went ahead:");
    for (size_t i = 0; i < shell.save_failed.size(); ++i) {
        const rv_editor_nvim_saved &f = shell.save_failed[i];
        const std::string label = f.id != 0 ? rv_editor_buffer_label(shell.app, f.name)
            : f.name.empty()                 ? std::string("nvim")
                                             : f.name;
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s: %s", label.c_str(), f.error.c_str());
        ImGui::PopStyleColor();
        if (f.id != 0 && f.name.empty() && shell.save_as_buffer == 0) {
            ImGui::PushID(static_cast<int>(i));
            if (rv_editor_button("Save As...", theme)) {
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
    const rv_editor_state busy = shell.saving ? rv_editor_state{ rv_editor_look::live, "Waiting for nvim to answer" }
                                              : rv_editor_state{};
    const int64_t win = app.nvim.window_for(shell.closing);
    const rv_editor_nvim_buffer *buf = app.nvim.buffer_in(win);
    const std::string name = buf == nullptr ? "This file" : rv_editor_buffer_label(app, buf->name);
    rv_editor_ask_begin("Unsaved Changes", theme);
    ImGui::TextWrapped("%s has unsaved changes. The tile closes once they are saved or discarded.", name.c_str());
    rv_editor_shell_failures(shell, theme);
    if (shell.save_as_buffer != 0) {
        rv_editor_shell_save_as_body(shell, theme);
    } else {
        const bool save = rv_editor_button("Save", theme, busy);
        ImGui::SameLine();
        const bool discard = rv_editor_button("Discard", theme, busy);
        ImGui::SameLine();
        const bool keep = rv_editor_button("Keep Open", theme);
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
    rv_editor_ask_begin("Save As", theme);
    rv_editor_shell_save_as_body(shell, theme);
    rv_editor_ask_end();
}

void rv_editor_page_review(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_app &app = shell.app;
    rv_editor_pane_header("Review Changes", true, theme);
    if (shell.leaving == leave::none) {
        ImGui::TextUnformatted("Nothing waits on unsaved files.");
        return;
    }
    const rv_editor_state busy = shell.saving ? rv_editor_state{ rv_editor_look::live, "Waiting for nvim to answer" }
                                              : rv_editor_state{};
    // Build, Run and Build and Restart go ahead without saving too: they use the files as on disk.
    const bool ahead = shell.leaving == leave::build || shell.leaving == leave::run || shell.leaving == leave::build_restart;
    const char *verb =
        shell.leaving == leave::build ? "Build" : shell.leaving == leave::run ? "Run" : "Build and Restart";
    ImGui::TextWrapped("%s", shell.leaving == leave::quit
            ? "The editor closes once these files are saved or discarded; Return keeps it open:"
            : shell.leaving == leave::open
            ? "Another project opens once these files are saved or discarded; Return stays here:"
            : "Builds use the files as saved. These have unsaved changes:");
    for (const rv_editor_nvim_buffer &b : app.nvim.modified()) {
        if (!ahead || !b.name.empty()) {
            ImGui::BulletText("%s", rv_editor_buffer_label(app, b.name).c_str());
        }
    }
    if (rv_editor_app_scene_dirty(app)) {
        ImGui::BulletText("%s (scene)", rv_editor_app_scene_name(app).c_str());
    }
    rv_editor_shell_failures(shell, theme);
    if (shell.save_as_buffer != 0) {
        rv_editor_shell_save_as_body(shell, theme);
        return;
    }
    if (ahead) {
        const bool save = rv_editor_button((std::string("Save and ") + verb).c_str(), theme, busy);
        ImGui::SameLine();
        const bool saved = rv_editor_button((std::string(verb) + " Saved Files").c_str(), theme, busy);
        ImGui::SameLine();
        const bool back = rv_editor_button("Return", theme);
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
    const bool save = rv_editor_button("Save All", theme, busy);
    ImGui::SameLine();
    const bool discard = rv_editor_button("Discard All", theme, busy);
    ImGui::SameLine();
    const bool back = rv_editor_button("Return", theme);
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
