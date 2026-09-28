// Runtime Controls and Project: views of the editor's models.

#include "panes/rv_editor_panes.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

rv_editor_status_kind rv_editor_run_lamp(const rv_editor_session &session)
{
    switch (session.state()) {
        case rv_editor_run_state::running: return rv_editor_status_kind::ok;
        case rv_editor_run_state::paused: return rv_editor_status_kind::warning;
        case rv_editor_run_state::crashed:
        case rv_editor_run_state::disconnected:
        case rv_editor_run_state::refused: return rv_editor_status_kind::error;
        case rv_editor_run_state::stopped:
        case rv_editor_run_state::exited: return rv_editor_status_kind::idle;
        default: return rv_editor_status_kind::busy;
    }
}

void rv_editor_wrapped(const std::string &text)
{
    ImGui::TextWrapped("%s", text.c_str());
}

} // namespace

void rv_editor_pane_controls(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_session &s = app.session;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    // Reload shows only while the running disc can take one (a directory with a Lua entry).
    const rv_editor_transport_state state{ rv_editor_app_why_not_build(app), rv_editor_app_why_not_run(app),
        rv_editor_app_why_not_pause(app), rv_editor_app_why_not_step(app), rv_editor_app_why_not_stop(app),
        rv_editor_app_why_not_reload(app), s.state() == rv_editor_run_state::paused, rv_editor_app_can_reload(app) };
    const rv_editor_transport_actions clicked = rv_editor_transport_bar(state, theme);
    if (clicked.build) {
        rv_editor_app_build(app);
    }
    if (clicked.run) {
        rv_editor_app_run(app);
    }
    if (clicked.pause) {
        rv_editor_app_pause(app);
    }
    if (clicked.step) {
        rv_editor_app_step(app);
    }
    if (clicked.stop) {
        rv_editor_app_stop(app);
    }
    if (clicked.reload) {
        rv_editor_app_reload(app);
    }

    // On the same row while it fits: the runtime's confirmed state, its frame and
    // what it runs, or, stopped, what Run would start.
    const char *name = rv_editor_run_state_name(s.state());
    rv_editor_flow(rv_editor_checkbox_width(name));
    rv_editor_status(name, rv_editor_run_lamp(s), theme);
    std::string facts;
    if (s.live()) {
        facts = "frame " + std::to_string(s.frame()) + " | session #" + std::to_string(s.number()) + " | build #" +
            std::to_string(s.build_number());
        if (s.facts().lua_budget > 0) {
            facts += " | script revision " + std::to_string(s.facts().revision) +
                (s.facts().revision != s.facts().first_revision ? " (reloaded)" : "");
        }
    } else if (app.build.last_success()) {
        facts = "Target: build #" + std::to_string(app.build.last_success()->number);
    } else {
        facts = "Target: nothing built yet";
    }
    rv_editor_flow(ImGui::CalcTextSize(facts.c_str()).x);
    ImGui::TextUnformatted(facts.c_str());
    const std::string profile = "Profile: " + app.run_config.profiles[app.run_config.active].name;
    rv_editor_flow(rv_editor_button_width(profile.c_str()));
    if (rv_editor_button(profile.c_str(), theme)) {
        app.run_config_open = true;
    }
    ImGui::SetItemTooltip("Run Configuration: how Run starts the runtime");

    // The last reload by the transport, as the runtime answered it (RLD-04).
    if (s.live() && rv_editor_app_can_reload(app)) {
        const bool on_save = app.run_config.profiles[app.run_config.active].reload_on_save;
        const char *label = s.reloading()      ? "Reloading"
            : !s.reload_result().empty()       ? (s.reload_ok() ? "Reload accepted" : "Reload refused")
            : on_save                          ? "Reload On Save"
                                               : nullptr;
        if (label != nullptr) {
            rv_editor_status(label, s.reloading() ? rv_editor_status_kind::busy
                    : s.reload_result().empty()   ? rv_editor_status_kind::idle
                    : s.reload_ok()               ? rv_editor_status_kind::ok
                                                  : rv_editor_status_kind::error,
                theme);
            if (!s.reloading() && !s.reload_result().empty()) {
                ImGui::SameLine();
                rv_editor_wrapped(s.reload_result());
            }
        }
    }

    if (!s.live() && !s.end_reason().empty()) {
        rv_editor_wrapped("Last run: " + s.end_reason());
    }
    if (s.uncertain()) {
        rv_editor_wrapped("A request went unanswered; the state shown is the last one the console confirmed.");
    }
    if (s.hung() || s.state() == rv_editor_run_state::disconnected) {
        if (rv_editor_button("Force Stop", theme)) {
            app.session.force_stop(app.log);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(s.hung() ? "the runtime did not end after quit" : "the channel is gone");
    }

    // The build's outcome is in the status bar and its lines in the Build Log;
    // here only what can be done about a running one.
    if (app.build.busy()) {
        const rv_editor_state cancel = app.build.state() == rv_editor_build_state::cancelling
            ? rv_editor_state{ rv_editor_look::live, "Already cancelling" }
            : rv_editor_state{};
        if (rv_editor_button("Cancel Build", theme, cancel)) {
            app.build.cancel();
        }
    }
}

void rv_editor_open_project_row(rv_editor_app &app, const rv_editor_theme &theme)
{
    ImGui::TextUnformatted("No project is open.");
    ImGui::SameLine();
    if (rv_editor_button("Open Project...", theme)) {
        app.open_folder_request = true;
    }
}

void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_project &p = app.project;
    if (!p.open) {
        rv_editor_wrapped("No project is open. File > Open Project... opens a game directory with its disc.toml.");
    } else {
        rv_editor_wrapped("Disc: " + (p.disc_id.empty() ? std::string("?") : p.disc_id) +
            (p.disc_title.empty() ? "" : " - " + p.disc_title));
        if (!p.manifest_error.empty()) {
            rv_editor_wrapped("disc.toml does not parse:\n" + p.manifest_error);
        }
        rv_editor_path_row("Root", p.root.string(), theme);
        rv_editor_path_row("Builds", (p.cache_dir / "builds").string(), theme);
        rv_editor_path_row("Memory card", (p.state_dir / "memcard.mppccard").string(), theme);
    }

    ImGui::SeparatorText("Toolchain");
    const rv_editor_tool *tools[] = { &app.tools.console, &app.tools.burner, &app.tools.baker };
    const char *names[] = { "Runtime", "Burner", "Baker" };
    for (int i = 0; i < 3; ++i) {
        const rv_editor_tool &t = *tools[i];
        rv_editor_status(names[i], t.problem.empty() ? rv_editor_status_kind::ok : rv_editor_status_kind::error, theme);
        ImGui::SameLine();
        if (!t.problem.empty()) {
            rv_editor_wrapped(t.problem);
            continue;
        }
        rv_editor_wrapped(t.version.empty() ? "found in " + t.origin : t.version + ", found in " + t.origin);
        ImGui::PushID(i);
        rv_editor_path_row("Path", t.path.string(), theme);
        ImGui::PopID();
    }
    const rv_editor_state look_again = app.build.busy() || app.session.live()
        ? rv_editor_state{ rv_editor_look::live, "Not while a build or the runtime is running" }
        : rv_editor_state{};
    if (rv_editor_button("Look for Tools Again", theme, look_again)) {
        rv_editor_app_init(app);
    }
    rv_editor_path_row("Settings", app.tools.settings_path.string(), theme);
    if (!app.tools.settings_error.empty()) {
        rv_editor_wrapped(app.tools.settings_error);
    }
}

} // namespace rv_editor
