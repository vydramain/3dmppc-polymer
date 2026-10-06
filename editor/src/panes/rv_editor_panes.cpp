// Runtime Controls and Project: views of the editor's models.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Reserved lines for the message area: displays unanswered request, detail, build/restart note, or last run reason.
constexpr int reserved_message_lines = 2;

// Builds subdirectory in the project cache. Same value used in rv_editor_build.cpp, rv_editor_app_logs.cpp.
constexpr std::string_view builds_dir_name = "builds";

// Memory card file in the project's state directory. Same value used in rv_editor_app.cpp.
constexpr std::string_view memcard_filename = "memcard.mppccard";

rv_editor_status_kind rv_editor_run_lamp(const rv_editor_session &session)
{
    switch (session.state()) {
    case rv_editor_run_state::running:
        return rv_editor_status_kind::active;
    case rv_editor_run_state::paused:
        return rv_editor_status_kind::warning;
    case rv_editor_run_state::crashed:
    case rv_editor_run_state::disconnected:
    case rv_editor_run_state::refused:
        return rv_editor_status_kind::error;
    case rv_editor_run_state::stopped:
    case rv_editor_run_state::exited:
        return rv_editor_status_kind::idle;
    default:
        return rv_editor_status_kind::busy;
    }
}

// Reload's disabled reason, always computed: "no session" and "disc cannot
// reload" first (the transport button stays visible either way), then the
// finer reasons rv_editor_app_why_not_reload already gives a live session.
const char *rv_editor_why_not_reload_shown(const rv_editor_app &app)
{
    if (!app.session.live()) {
        return rv_editor_text("panes.no_session_running");
    }
    if (!rv_editor_app_can_reload(app)) {
        return rv_editor_text("panes.disc_no_lua_entry");
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
    const std::string why_not_reload = !can_reload ? rv_editor_text("panes.disc_cannot_reload") : plan.reason;
    std::string reload_name;
    if (plan.action == rv_editor_change_action::reload_module) {
        reload_name = rv_editor_text_format("panes.reload_module", std::make_format_args(plan.name));
    } else if (plan.action == rv_editor_change_action::refresh_texture) {
        reload_name = rv_editor_text_format("panes.refresh_texture", std::make_format_args(plan.name));
    } else {
        reload_name = rv_editor_text("panes.reload_entry_script");
    }
    // Reload is always shown; it is disabled with a reason when the disc cannot take one.
    const rv_editor_transport_state state{ rv_editor_app_why_not_build(app),
        rv_editor_app_why_not_run(app),
        rv_editor_app_why_not_pause(app),
        rv_editor_app_why_not_step(app),
        rv_editor_app_why_not_stop(app),
        rv_editor_why_not_reload_shown(app),
        s.state() == rv_editor_run_state::paused,
        offer_build_restart,
        offer_build_restart ? rv_editor_app_why_not_build(app) : nullptr,
        reload_name.c_str() };
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
        offer_build_restart ? why_not_reload + " " + rv_editor_text("panes.restart_loses_state") : std::string();

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
            badge_label = rv_editor_text_format("panes.baking_texture", std::make_format_args(baking_name));
            badge_kind = rv_editor_status_kind::active;
        } else if (bake_failed) {
            badge_label = rv_editor_text("panes.bake_failed");
            badge_kind = rv_editor_status_kind::error;
            detail = app.texture_bake.message;
        } else if (s.reloading()) {
            badge_label = rv_editor_text("panes.reloading");
            badge_kind = rv_editor_status_kind::active;
        } else if (!s.reload_result().empty()) {
            badge_label = s.reload_ok() ? rv_editor_text("panes.reload_accepted") : rv_editor_text("panes.reload_refused");
            badge_kind = s.reload_ok() ? rv_editor_status_kind::ok : rv_editor_status_kind::error;
            detail = s.reload_result();
        } else if (on_save) {
            badge_label = rv_editor_text("panes.reload_on_save");
        }
    }
    const std::string end_reason = s.end_reason();
    std::string last_run_message;
    if (!s.live() && !end_reason.empty()) {
        last_run_message = rv_editor_text_format("panes.last_run", std::make_format_args(end_reason));
    }

    // One message area, 2 lines: the strip has many things it might say but
    // never more than one matters at once, by priority (an unanswered request
    // outranks a stale reload detail, which outranks the reload/build-restart
    // note, which outranks what the last run ended with).
    std::string message;
    if (s.uncertain()) {
        message = rv_editor_text("panes.request_unanswered");
    } else if (!detail.empty()) {
        message = detail;
    } else if (!build_restart_message.empty()) {
        message = build_restart_message;
    } else {
        message = last_run_message;
    }
    rv_editor_reserved("##message", message, reserved_message_lines);

    // Facts/target and the profile button share one row: facts clips rather
    // than wraps, so this row can never split in two.
    const bool debug_preset = app.preset == rv_editor_layout_preset::debug;
    std::string facts;
    if (s.live() && !debug_preset) {
        const int frame_num = s.frame();
        const int session_num = s.number();
        const int build_num = s.build_number();
        facts = rv_editor_text_format("panes.facts_live", std::make_format_args(frame_num, session_num, build_num));
        if (s.facts().lua_budget > 0) {
            const int revision = s.facts().revision;
            const int first_revision = s.facts().first_revision;
            if (revision != first_revision) {
                facts += rv_editor_text_format("panes.script_revision_reloaded", std::make_format_args(revision));
            } else {
                facts += rv_editor_text_format("panes.script_revision", std::make_format_args(revision));
            }
        }
    } else if (!s.live() && app.build.last_success()) {
        const int build_number = app.build.last_success()->number;
        facts = rv_editor_text_format("panes.target_build", std::make_format_args(build_number));
    } else if (!s.live()) {
        facts = rv_editor_text("panes.target_nothing_built");
    }
    const std::string profile_name = app.run_config.profiles[app.run_config.active].name;
    const std::string profile = rv_editor_text_format("panes.profile", std::make_format_args(profile_name));
    const float profile_w = rv_editor_button_width(profile.c_str());
    const float facts_w = std::max(1.0f, ImGui::GetContentRegionAvail().x - profile_w - ImGui::GetStyle().ItemSpacing.x);
    rv_editor_reserved_line("##facts", facts, facts_w);
    ImGui::SameLine();
    if (rv_editor_button(profile.c_str(), theme)) {
        app.show_request = rv_editor_pane_kind::run_config;
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("panes.run_config_tooltip"));

    // One action row shared by the three things it can offer, by priority: a
    // hung runtime needs Force Stop first, then a running build needs Cancel,
    // and only then the reload badge, the least urgent of the three.
    if (s.hung() || s.state() == rv_editor_run_state::disconnected) {
        if (rv_editor_button(rv_editor_text("panes.force_stop_button"), theme)) {
            app.session.force_stop(app.log);
        }
        ImGui::SameLine();
        const char *msg = s.hung() ? rv_editor_text("panes.runtime_not_ended") : rv_editor_text("panes.channel_gone");
        ImGui::TextUnformatted(msg);
    } else if (app.build.busy()) {
        rv_editor_state cancel;
        if (app.build.state() == rv_editor_build_state::cancelling) {
            cancel = rv_editor_state{ rv_editor_look::live, rv_editor_text("panes.already_cancelling") };
        }
        if (rv_editor_button(rv_editor_text("panes.cancel_build_button"), theme, cancel)) {
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
    ImGui::TextUnformatted(rv_editor_text("panes.no_project_open_message"));
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("panes.open_project_button"), theme)) {
        app.open_folder_request = true;
    }
}

void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme)
{
    // Controls sit inside the content between its sections: the whole pane is one well.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const rv_editor_project &p = app.project;
    if (!p.open) {
        rv_editor_wrapped(rv_editor_text("panes.no_project_open_full"));
    } else {
        rv_editor_project_group(rv_editor_text("panes.disc_heading"), theme);
        const std::string disc_id = p.disc_id.empty() ? rv_editor_text("panes.disc_id_unknown") : p.disc_id;
        const std::string disc_title = p.disc_title;
        std::string disc_info;
        if (p.disc_title.empty()) {
            disc_info = rv_editor_text_format("panes.disc_info", std::make_format_args(disc_id));
        } else {
            disc_info = rv_editor_text_format("panes.disc_info_with_title", std::make_format_args(disc_id, disc_title));
        }
        rv_editor_wrapped(disc_info);
        if (!p.manifest_error.empty()) {
            rv_editor_wrapped(rv_editor_text("panes.manifest_parse_error") + p.manifest_error);
        }
    }

    // The editor's own paths and tools, not disc.toml sections.
    rv_editor_project_group(rv_editor_text("panes.editor_heading"), theme);
    if (p.open) {
        rv_editor_path_row(rv_editor_text("panes.root_path"), p.root.string(), theme);
        rv_editor_path_row(rv_editor_text("panes.builds_path"), (p.cache_dir / builds_dir_name).string(), theme);
        rv_editor_path_row(rv_editor_text("panes.memory_card_path"), (p.state_dir / memcard_filename).string(), theme);
    }

    ImGui::SeparatorText(rv_editor_text("panes.toolchain_header"));
    const rv_editor_tool *tools[] = { &app.tools.console, &app.tools.burner, &app.tools.baker };
    const char *tool_names[] = { rv_editor_text("panes.tool_runtime"),
        rv_editor_text("panes.tool_burner"),
        rv_editor_text("panes.tool_baker") };
    for (size_t i = 0; i < std::size(tools); ++i) {
        const rv_editor_tool &t = *tools[i];
        rv_editor_status(tool_names[i], t.problem.empty() ? rv_editor_status_kind::ok : rv_editor_status_kind::error, theme);
        ImGui::SameLine();
        if (!t.problem.empty()) {
            rv_editor_wrapped(t.problem);
            continue;
        }
        const std::string origin = t.origin;
        const std::string origin_text = rv_editor_text_format("panes.tool_found_in", std::make_format_args(origin));
        rv_editor_wrapped(t.version.empty() ? origin_text : t.version + ", " + origin_text);
        ImGui::PushID(i);
        rv_editor_path_row(rv_editor_text("panes.path_label"), t.path.string(), theme);
        ImGui::PopID();
    }
    rv_editor_state look_again;
    if (app.build.busy() || app.session.live()) {
        look_again = rv_editor_state{ rv_editor_look::live, rv_editor_text("panes.tools_not_while_running") };
    }
    if (rv_editor_button(rv_editor_text("panes.look_for_tools_button"), theme, look_again)) {
        rv_editor_app_init(app);
    }
    rv_editor_path_row(rv_editor_text("panes.settings_path"), app.tools.settings_path.string(), theme);
    if (!app.tools.settings_error.empty()) {
        rv_editor_wrapped(app.tools.settings_error);
    }
    rv_editor_well_end();
}

} // namespace rv_editor
