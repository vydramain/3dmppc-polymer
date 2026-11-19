// Runtime Controls: the transport bar, its action slot and the message row.

#include "panes/rv_editor_pane_controls.hpp"

#include <algorithm>
#include <format>
#include <string>

#include "imgui.h"

#include "panes/rv_editor_pane_mark_tools.hpp"
#include "panes/rv_editor_panes.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_steps.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

rv_editor_controls_message_text rv_editor_controls_message(const rv_editor_controls_message_input &in)
{
    if (in.force_stop) {
        return { rv_editor_text(in.hung ? "panes.brief_hung" : "panes.brief_channel_gone"),
            rv_editor_text(in.hung ? "panes.runtime_not_ended" : "panes.channel_gone") };
    }
    if (in.uncertain) {
        return { rv_editor_text("panes.brief_unanswered"), rv_editor_text("panes.request_unanswered") };
    }
    if (!in.detail.empty()) {
        const char *brief = "panes.brief_detail";
        switch (in.outcome) {
        case rv_editor_controls_outcome::reload_accepted:
            brief = "panes.brief_reload_accepted";
            break;
        case rv_editor_controls_outcome::reload_refused:
            brief = "panes.brief_reload_refused";
            break;
        case rv_editor_controls_outcome::bake_failed:
            brief = "panes.brief_bake_failed";
            break;
        case rv_editor_controls_outcome::none:
            break;
        }
        return { rv_editor_text(brief), in.detail };
    }
    if (in.offer_build_restart) {
        // The disc itself refusing to reload wins over the plan's wording, which may name a module reload.
        const std::string why = in.disc_cannot_reload ? rv_editor_text("panes.disc_cannot_reload") : in.why_not_reload;
        return { rv_editor_text(in.disc_cannot_reload ? "panes.brief_disc_cannot_reload" : "panes.brief_build_restart"),
            why + " " + rv_editor_text("panes.restart_loses_state") };
    }
    return {};
}

std::string rv_editor_facts_line(const rv_editor_facts_input &in)
{
    if (!in.live || in.debug_preset) {
        return {};
    }
    std::string result = rv_editor_text_format("panes.facts_live", std::make_format_args(in.frame, in.session, in.build));
    if (!in.has_lua) {
        return result;
    }
    const bool reloaded = in.revision != in.first_revision;
    return result +
        rv_editor_text_format(reloaded ? "panes.script_revision_reloaded" : "panes.script_revision",
            std::make_format_args(in.revision));
}

namespace
{

// What the action slot after the state name offers; Cancel Build is the one it falls back to, disabled when no build runs.
enum class rv_editor_action_kind {
    force_stop,
    cancel_build,
    reload_badge,
};

// A hung or lost runtime needs Force Stop first, then a running build needs
// Cancel, then the reload badge; with none of them Cancel Build stays, disabled.
rv_editor_action_kind rv_editor_action_for(const rv_editor_session &session, bool building, bool has_badge)
{
    if (session.hung() || session.state() == rv_editor_run_state::disconnected) {
        return rv_editor_action_kind::force_stop;
    }
    if (building || !has_badge) {
        return rv_editor_action_kind::cancel_build;
    }
    return rv_editor_action_kind::reload_badge;
}

// Draws the action slot's item: the button or the badge; `none` draws nothing.
void rv_editor_action_draw(rv_editor_app &app,
    rv_editor_action_kind action,
    const std::string &badge_label,
    rv_editor_status_kind badge_kind,
    const rv_editor_theme &theme)
{
    switch (action) {
    case rv_editor_action_kind::force_stop: {
        const bool force_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::force_stop);
        const char *force_why = force_pending ? rv_editor_text("app_why_not.stop_pending") : nullptr;
        if (rv_editor_button(rv_editor_text("panes.force_stop_button"), theme, { rv_editor_look::live, force_why })) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::force_stop });
        }
        break;
    }
    case rv_editor_action_kind::cancel_build: {
        const bool cancel_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cancel_build);
        const bool cancelling = app.build.state() == rv_editor_build_state::cancelling;
        const char *cancelling_why = cancelling ? rv_editor_text("panes.already_cancelling") : nullptr;
        const char *idle_why = app.build.busy() ? nullptr : rv_editor_text("panes.no_build_to_cancel");
        const char *busy_why = cancel_pending ? rv_editor_text("app_why_not.cancel_pending") : cancelling_why;
        const char *cancel_why = idle_why != nullptr ? idle_why : busy_why;
        if (rv_editor_button(rv_editor_text("panes.cancel_build_button"), theme, { rv_editor_look::live, cancel_why })) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::cancel_build });
        }
        // The reason shows at once, not after the hover delay: it is why the slot is greyed.
        if (cancel_why != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            rv_editor_tooltip(cancel_why);
        }
        break;
    }
    case rv_editor_action_kind::reload_badge:
        rv_editor_status(badge_label.c_str(), badge_kind, theme);
        break;
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

// One reserved single line, `width` wide (0 = what remains of the row): `text`
// clips rather than wraps, so this row never splits into two. Hovering shows
// `full`, or `text` in full when `full` is empty.
void rv_editor_reserved_line(const char *id,
    const std::string &text,
    float width,
    const rv_editor_theme &theme,
    const std::string &full = {})
{
    rv_editor_message_line(id, text.c_str(), 1, width, theme, full.c_str());
}

} // namespace

float rv_editor_controls_action_slot_width(const rv_editor_theme &theme)
{
    float width = std::max(rv_editor_button_width(rv_editor_text("panes.force_stop_button")),
        rv_editor_button_width(rv_editor_text("panes.cancel_build_button")));
    for (const char *key :
        { "panes.reload_accepted", "panes.reload_refused", "panes.reloading", "panes.reload_on_save", "panes.bake_failed" }) {
        for (const auto kind : { rv_editor_status_kind::ok, rv_editor_status_kind::error, rv_editor_status_kind::active }) {
            width = std::max(width, rv_editor_status_width(rv_editor_text(key), kind, theme));
        }
    }
    return width;
}

void rv_editor_pane_controls(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &)
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
    std::string reload_name;
    if (plan.action == rv_editor_change_action::reload_module) {
        reload_name = rv_editor_text_format("panes.reload_module", std::make_format_args(plan.name));
    } else if (plan.action == rv_editor_change_action::refresh_texture) {
        reload_name = rv_editor_text_format("panes.refresh_texture", std::make_format_args(plan.name));
    } else {
        reload_name = rv_editor_text("panes.reload_entry_script");
    }
    // Reload is always shown; it is disabled with a reason when the disc cannot take one.
    // A Stop waiting for the next frame blocks the bar's Stop, saying so in place of the app's reason.
    const bool stop_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::stop);
    const rv_editor_transport_state state{ rv_editor_app_why_not_build(app),
        rv_editor_app_why_not_run(app),
        rv_editor_app_why_not_pause(app),
        rv_editor_app_why_not_step(app),
        stop_pending ? rv_editor_text("app_why_not.stop_pending") : rv_editor_app_why_not_stop(app),
        rv_editor_why_not_reload_shown(app),
        s.state() == rv_editor_run_state::paused,
        offer_build_restart,
        offer_build_restart ? rv_editor_app_why_not_build(app) : nullptr,
        reload_name.c_str() };
    // The last reload by the transport, as the runtime answered it. A
    // texture bake's own state comes first: it is what Reload is waiting on, and
    // a bake that failed never reached the console, so its message would be lost
    // behind an older console answer otherwise.
    std::string badge_label;
    rv_editor_status_kind badge_kind = rv_editor_status_kind::idle;
    std::string detail;
    rv_editor_controls_outcome outcome = rv_editor_controls_outcome::none;
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
            outcome = rv_editor_controls_outcome::bake_failed;
        } else if (s.reloading()) {
            badge_label = rv_editor_text("panes.reloading");
            badge_kind = rv_editor_status_kind::active;
        } else if (!s.reload_result().empty()) {
            badge_label = s.reload_ok() ? rv_editor_text("panes.reload_accepted") : rv_editor_text("panes.reload_refused");
            badge_kind = s.reload_ok() ? rv_editor_status_kind::ok : rv_editor_status_kind::error;
            detail = s.reload_result();
            outcome = s.reload_ok() ? rv_editor_controls_outcome::reload_accepted : rv_editor_controls_outcome::reload_refused;
        } else if (on_save) {
            badge_label = rv_editor_text("panes.reload_on_save");
        }
    }

    // The action slot and the mark note follow the transport buttons on the bar's one row; the tile's minimum
    // width (rv_editor_transport_min_width) holds them all.
    const rv_editor_action_kind action = rv_editor_action_for(s, app.build.busy(), !badge_label.empty());
    const rv_editor_transport_actions clicked = rv_editor_transport_bar(state, theme, rv_editor_transport_column_min_width());
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
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::stop });
    }
    if (clicked.reload) {
        rv_editor_app_reload(app);
    }
    if (clicked.build_restart) {
        rv_editor_app_build_restart(app);
    }
    // The right column beside the tiles has two rows: the action slot with the mark note, then the message row. The
    // slot is always drawn, at the one width, in a clipping area, so what it holds never moves the items after it.
    const float slot_w = rv_editor_controls_action_slot_width(theme);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##action", ImVec2(slot_w, ImGui::GetFrameHeight()), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    rv_editor_action_draw(app, action, badge_label, badge_kind, theme);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    // The note fills the column's first row. With small buttons the column is never narrower than its least width, so
    // the strip's minimum is real; with large ones the column takes what the 640 px row leaves it.
    const bool small_buttons =
        rv_editor_transport_side(ImGui::GetWindowWidth(), rv_editor_transport_column_min_width()) <= ImGui::GetFrameHeight();
    const float note_floor = small_buttons ? rv_editor_transport_column_min_width() - slot_w - ImGui::GetStyle().ItemSpacing.x :
                                             rv_editor_mark_note_min_width();
    rv_editor_mark_note(app, theme, std::max(note_floor, ImGui::GetContentRegionAvail().x));

    rv_editor_controls_message_text message = rv_editor_controls_message({ action == rv_editor_action_kind::force_stop,
        s.hung(),
        s.uncertain(),
        detail,
        outcome,
        offer_build_restart,
        !can_reload,
        plan.reason });
    // A build's steps fill the line when nothing more urgent does.
    if (message.brief.empty() && (app.build.busy() || app.build.state() == rv_editor_build_state::failed)) {
        message.brief = rv_editor_steps_compact(app.build.progress());
    }

    // The second row is the message alone, never narrower than the longest message; the facts and the profile live elsewhere.
    const float message_w = std::max(rv_editor_transport_column_min_width(), ImGui::GetContentRegionAvail().x);
    rv_editor_reserved_line("##message", message.brief, message_w, theme, message.full);
    ImGui::EndGroup();
    rv_editor_shelf_end();
}

} // namespace rv_editor
