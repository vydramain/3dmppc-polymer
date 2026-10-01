// Runtime Controls and Project: views of the editor's models.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
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

// One reserved block of `lines` wrapped-text lines, whatever `text` is: a
// strip's row must not change size with what it has to say. Text past the
// reserved lines clips; hovering the block shows it in full.
void rv_editor_reserved(const char *id, const std::string &text, int lines)
{
    const float h = ImGui::GetTextLineHeightWithSpacing() * lines;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (!text.empty()) {
        ImGui::TextWrapped("%s", text.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (!text.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.c_str());
    }
}

// One reserved single line, `width` wide (0 = what remains of the row): `text`
// clips rather than wraps, so this row never splits into two. Hovering shows
// it in full.
void rv_editor_reserved_line(const char *id, const std::string &text, float width)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild(id, ImVec2(width, ImGui::GetFrameHeight()), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (!text.empty()) {
        ImGui::TextUnformatted(text.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (!text.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.c_str());
    }
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
    const std::string build_restart_message =
        offer_build_restart ? why_not_reload + " Restarting loses the game's current state." : std::string();

    // The runtime's confirmed state follows the transport bar on the same row;
    // the bar hides its own buttons first rather than let that status get clipped.
    rv_editor_flow(name_width);
    rv_editor_status(name, name_kind, theme);

    // The last reload by the transport, as the runtime answered it. A
    // texture bake's own state comes first: it is what Reload is waiting on, and
    // a bake that failed never reached the console, so its message would be lost
    // behind an older console answer otherwise.
    std::string badge_label;
    rv_editor_status_kind badge_kind = rv_editor_status_kind::idle;
    std::string detail;
    if (s.live() && rv_editor_app_can_reload(app)) {
        std::string baking_name;
        const bool baking = rv_editor_app_texture_bake_busy(app, &baking_name);
        // A failed bake stays shown until the next bake or a session
        // restart; a later unrelated console reload does not clear it. Upgrade
        // path: a reload-attempt sequence number if that ordering matters.
        const bool bake_failed = !baking && app.texture_bake.proc == nullptr && !app.texture_bake.ok &&
            !app.texture_bake.message.empty() && app.texture_bake.build_number == s.build_number();
        const bool on_save = app.run_config.profiles[app.run_config.active].reload_on_save;
        if (baking) {
            badge_label = "Baking texture " + baking_name;
            badge_kind = rv_editor_status_kind::active;
        } else if (bake_failed) {
            badge_label = "Bake failed";
            badge_kind = rv_editor_status_kind::error;
            detail = app.texture_bake.message;
        } else if (s.reloading()) {
            badge_label = "Reloading";
            badge_kind = rv_editor_status_kind::active;
        } else if (!s.reload_result().empty()) {
            badge_label = s.reload_ok() ? "Reload accepted" : "Reload refused";
            badge_kind = s.reload_ok() ? rv_editor_status_kind::ok : rv_editor_status_kind::error;
            detail = s.reload_result();
        } else if (on_save) {
            badge_label = "Reload On Save";
        }
    }
    const std::string last_run_message =
        !s.live() && !s.end_reason().empty() ? "Last run: " + s.end_reason() : std::string();

    // One message area, 2 lines: the strip has many things it might say but
    // never more than one matters at once, by priority (an unanswered request
    // outranks a stale reload detail, which outranks the reload/build-restart
    // note, which outranks what the last run ended with).
    const std::string message = s.uncertain()
        ? "A request went unanswered; the state shown is the last one the console confirmed."
        : !detail.empty()                 ? detail
        : !build_restart_message.empty()  ? build_restart_message
                                           : last_run_message;
    rv_editor_reserved("##message", message, 2);

    // Facts/target and the profile button share one row: facts clips rather
    // than wraps, so this row can never split in two.
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
    const std::string profile = "Profile: " + app.run_config.profiles[app.run_config.active].name;
    const float profile_w = rv_editor_button_width(profile.c_str());
    const float facts_w = std::max(1.0f, ImGui::GetContentRegionAvail().x - profile_w - ImGui::GetStyle().ItemSpacing.x);
    rv_editor_reserved_line("##facts", facts, facts_w);
    ImGui::SameLine();
    if (rv_editor_button(profile.c_str(), theme)) {
        app.show_request = rv_editor_pane_kind::run_config;
    }
    ImGui::SetItemTooltip("Run Configuration: how Run starts the runtime");

    // One action row shared by the three things it can offer, by priority: a
    // hung runtime needs Force Stop first, then a running build needs Cancel,
    // and only then the reload badge, the least urgent of the three.
    if (s.hung() || s.state() == rv_editor_run_state::disconnected) {
        if (rv_editor_button("Force Stop", theme)) {
            app.session.force_stop(app.log);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(s.hung() ? "the runtime did not end after quit" : "the channel is gone");
    } else if (app.build.busy()) {
        const rv_editor_state cancel = app.build.state() == rv_editor_build_state::cancelling
            ? rv_editor_state{ rv_editor_look::live, "Already cancelling" }
            : rv_editor_state{};
        if (rv_editor_button("Cancel Build", theme, cancel)) {
            app.build.cancel();
        }
    } else if (!badge_label.empty()) {
        rv_editor_status(badge_label.c_str(), badge_kind, theme);
    } else {
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetFrameHeight()));
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
