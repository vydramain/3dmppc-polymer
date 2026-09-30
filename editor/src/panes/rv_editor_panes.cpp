// Runtime Controls and Project: views of the editor's models.

#include "panes/rv_editor_panes.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

rv_editor_status_kind rv_editor_run_lamp(const rv_editor_session &session)
{
    switch (session.state()) {
        case rv_editor_run_state::running: return rv_editor_status_kind::active;
        case rv_editor_run_state::paused: return rv_editor_status_kind::warning;
        case rv_editor_run_state::crashed:
        case rv_editor_run_state::disconnected:
        case rv_editor_run_state::refused: return rv_editor_status_kind::error;
        case rv_editor_run_state::stopped:
        case rv_editor_run_state::exited: return rv_editor_status_kind::idle;
        default: return rv_editor_status_kind::busy;
    }
}

// Reload's disabled reason, always computed: "no session" and "disc cannot
// reload" first (the transport button stays visible either way), then the
// finer reasons rv_editor_app_why_not_reload already gives a live session.
const char *rv_editor_why_not_reload_shown(const rv_editor_app &app)
{
    if (!app.session.live()) {
        return "No session is running";
    }
    if (!rv_editor_app_can_reload(app)) {
        return "This disc has no Lua entry script to reload";
    }
    return rv_editor_app_why_not_reload(app);
}

void rv_editor_wrapped(const std::string &text)
{
    ImGui::TextWrapped("%s", text.c_str());
}

// A group heading: the label in the bright text color, then a full-width separator.
void rv_editor_project_group(const char *label, const rv_editor_theme &theme)
{
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

} // namespace

void rv_editor_pane_controls(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_session &s = app.session;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    // Only controls, no separate content: the whole pane is a shelf.
    rv_editor_shelf_begin("##shelf", theme);
    // Reload's target while the session is live: the running build's map says
    // whether it can apply the pending change, or Build and Restart takes its slot.
    const bool can_reload = s.live() && rv_editor_app_can_reload(app);
    const rv_editor_change_plan plan = can_reload ? rv_editor_app_change_for(app, app.code_file) : rv_editor_change_plan{};
    const bool offer_build_restart = s.live() &&
        (!can_reload || plan.action == rv_editor_change_action::build_restart ||
            plan.action == rv_editor_change_action::restart_required);
    // The disc itself refusing to reload always wins over the plan's own wording
    // (which may name a module reload that the disc could never do).
    const std::string why_not_reload = !can_reload
        ? "The running disc cannot reload: it runs from an image or has no Lua entry script."
        : plan.reason;
    const std::string reload_name = plan.action == rv_editor_change_action::reload_module
        ? "Reload Module: " + plan.name
        : plan.action == rv_editor_change_action::refresh_texture ? "Refresh Texture: " + plan.name
                                                                    : "Reload Entry Script";
    // Reload is always shown; it is disabled with a reason when the disc cannot take one.
    const rv_editor_transport_state state{ rv_editor_app_why_not_build(app), rv_editor_app_why_not_run(app),
        rv_editor_app_why_not_pause(app), rv_editor_app_why_not_step(app), rv_editor_app_why_not_stop(app),
        rv_editor_why_not_reload_shown(app), s.state() == rv_editor_run_state::paused, offer_build_restart,
        offer_build_restart ? rv_editor_app_why_not_build(app) : nullptr, reload_name.c_str() };
    // The state name follows on the same row; the bar hides its own buttons first
    // rather than let that status get clipped.
    const char *name = rv_editor_run_state_name(s.state());
    const rv_editor_status_kind name_kind = rv_editor_run_lamp(s);
    const float name_width = rv_editor_status_width(name, name_kind, theme);
    const rv_editor_transport_actions clicked = rv_editor_transport_bar(state, theme, name_width);
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
    if (clicked.build_restart) {
        rv_editor_app_build_restart(app);
    }
    // Build and Restart took Reload's slot: say why, plainly, not only in its tooltip.
    // why_not_reload already ends in ".": add the state-loss warning as its own sentence.
    if (offer_build_restart) {
        rv_editor_wrapped(why_not_reload + " Restarting loses the game's current state.");
    }

    // On the same row while it fits: the runtime's confirmed state, its frame and
    // what it runs, or, stopped, what Run would start.
    rv_editor_flow(name_width);
    rv_editor_status(name, name_kind, theme);
    // Debug's Session pane already shows session/build/frame; the strip keeps
    // only the indicator above and drops that repeat.
    const bool debug_preset = app.preset == rv_editor_layout_preset::debug;
    std::string facts;
    if (s.live() && !debug_preset) {
        facts = "frame " + std::to_string(s.frame()) + " | session #" + std::to_string(s.number()) + " | build #" +
            std::to_string(s.build_number());
        if (s.facts().lua_budget > 0) {
            facts += " | script revision " + std::to_string(s.facts().revision) +
                (s.facts().revision != s.facts().first_revision ? " (reloaded)" : "");
        }
    } else if (!s.live() && app.build.last_success()) {
        facts = "Target: build #" + std::to_string(app.build.last_success()->number);
    } else if (!s.live()) {
        facts = "Target: nothing built yet";
    }
    if (!facts.empty()) {
        rv_editor_flow(ImGui::CalcTextSize(facts.c_str()).x);
        ImGui::TextUnformatted(facts.c_str());
    }
    const std::string profile = "Profile: " + app.run_config.profiles[app.run_config.active].name;
    rv_editor_flow(rv_editor_button_width(profile.c_str()));
    if (rv_editor_button(profile.c_str(), theme)) {
        app.show_request = rv_editor_pane_kind::run_config;
    }
    ImGui::SetItemTooltip("Run Configuration: how Run starts the runtime");

    // The last reload by the transport, as the runtime answered it (RLD-04). A
    // texture bake's own state comes first: it is what Reload is waiting on, and
    // a bake that failed never reached the console, so its message would be lost
    // behind an older console answer otherwise.
    if (s.live() && rv_editor_app_can_reload(app)) {
        std::string baking_name;
        const bool baking = rv_editor_app_texture_bake_busy(app, &baking_name);
        // ponytail: a failed bake stays shown until the next bake or a session
        // restart; a later unrelated console reload does not clear it. Upgrade
        // path: a reload-attempt sequence number if that ordering matters.
        const bool bake_failed = !baking && app.texture_bake.proc == nullptr && !app.texture_bake.ok &&
            !app.texture_bake.message.empty() && app.texture_bake.build_number == s.build_number();
        const bool on_save = app.run_config.profiles[app.run_config.active].reload_on_save;
        if (baking) {
            rv_editor_status(("Baking texture " + baking_name).c_str(), rv_editor_status_kind::active, theme);
        } else if (bake_failed) {
            rv_editor_status("Bake failed", rv_editor_status_kind::error, theme);
            ImGui::SameLine();
            rv_editor_wrapped(app.texture_bake.message);
        } else {
            const char *label = s.reloading()      ? "Reloading"
                : !s.reload_result().empty()       ? (s.reload_ok() ? "Reload accepted" : "Reload refused")
                : on_save                          ? "Reload On Save"
                                                   : nullptr;
            if (label != nullptr) {
                rv_editor_status(label, s.reloading() ? rv_editor_status_kind::active
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
    rv_editor_shelf_end();
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
    // Controls sit inside the content between its sections: the whole pane is one well.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const rv_editor_project &p = app.project;
    if (!p.open) {
        rv_editor_wrapped("No project is open. File > Open Project... opens a game directory with its disc.toml.");
    } else {
        rv_editor_project_group("Disc", theme);
        rv_editor_wrapped("Disc: " + (p.disc_id.empty() ? std::string("?") : p.disc_id) +
            (p.disc_title.empty() ? "" : " - " + p.disc_title));
        if (!p.manifest_error.empty()) {
            rv_editor_wrapped("disc.toml does not parse:\n" + p.manifest_error);
        }
    }

    // The editor's own paths and tools, not disc.toml sections.
    rv_editor_project_group("Editor", theme);
    if (p.open) {
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
    rv_editor_well_end();
}

} // namespace rv_editor
