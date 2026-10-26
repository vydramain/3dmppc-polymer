// Toolchest: Scene's tools for the scene document in front; in Debug, the
// session's: Mark Moment, Capture Frame, Report Issue and Restart.

#include "panes/rv_editor_panes.hpp"

#include <limits>
#include <string>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_field_sizes.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

// The draft of the Toolchest's one field; the add_mark request takes a copy of it.
char mark_draft[mark_note_field_size] = {};
// The marks count when a mark request took the draft; the draft clears once that request has run and
// the count grew. no_mark_sent: nothing is waiting.
constexpr size_t no_mark_sent = std::numeric_limits<size_t>::max();
size_t mark_count_at_send = no_mark_sent;

void rv_editor_session_tools(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    rv_editor_session &s = app.session;
    const char *no_session = s.live() ? nullptr : rv_editor_text("pane_toolchest.no_session");
    const bool mark_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::add_mark);
    const bool mark_done = mark_count_at_send != no_mark_sent && !mark_pending;
    // A refused mark (the app logged why) leaves the draft for another try.
    if (mark_done && app.marks.size() > mark_count_at_send) {
        mark_draft[0] = '\0';
    }
    if (mark_done) {
        mark_count_at_send = no_mark_sent;
    }
    const char *mark_why = mark_pending ? rv_editor_text("app_why_not.request_pending") : no_session;
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##mark_note", mark_draft, sizeof(mark_draft), theme, { {}, mark_pending });
    if (mark_draft[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + pad.x, min.y + pad.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            rv_editor_text("pane_toolchest.mark_placeholder"));
    }
    if (rv_editor_tool_button(rv_editor_text("pane_toolchest.mark_button"),
            rv_editor_glyph::mark_moment,
            theme.code_yellow,
            rv_editor_text("pane_toolchest.mark_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, mark_why })) {
        const rv_editor_pane_request request = { rv_editor_pane_request_kind::add_mark, rv_editor_tile_none, {}, mark_draft };
        if (rv_editor_app_request(app, request) == RV_OK) {
            mark_count_at_send = app.marks.size();
        }
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_toolchest.capture_button")));
    const bool capture_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::capture_frame);
    const char *capture_why = capture_pending ? rv_editor_text("app_why_not.request_pending") : no_session;
    if (rv_editor_tool_button(rv_editor_text("pane_toolchest.capture_button"),
            rv_editor_glyph::capture_frame,
            theme.code_blue,
            rv_editor_text("pane_toolchest.capture_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, capture_why })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::capture_frame });
    }
    // The moment is kept before the form is: a frame, the keyboard back, then Findings.
    const char *no_report = s.number() != 0 ? nullptr : rv_editor_text("pane_toolchest.no_report");
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_toolchest.report_button")));
    const bool report_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::report_problem);
    const char *report_why = report_pending ? rv_editor_text("app_why_not.request_pending") : no_report;
    if (rv_editor_tool_button(rv_editor_text("pane_toolchest.report_button"),
            rv_editor_glyph::report_issue,
            theme.code_red,
            rv_editor_text("pane_toolchest.report_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, report_why })) {
        const bool queued = rv_editor_app_request(app, { rv_editor_pane_request_kind::report_problem }) == RV_OK;
        if (queued) {
            ui.requests.push_back({ .kind = rv_editor_ui_request_kind::release_keyboard });
        }
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_toolchest.restart_button")));
    // A restart in progress says so in place of the no-session reason.
    const bool restart_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::restart);
    const char *restart_why = restart_pending ? rv_editor_text("app_why_not.restart_pending") : no_session;
    if (rv_editor_tool_button(rv_editor_text("pane_toolchest.restart_button"),
            rv_editor_glyph::restart,
            theme.code_green,
            rv_editor_text("pane_toolchest.restart_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, restart_why })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::restart });
    }
}

} // namespace

void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    // Only controls, no separate content: the whole pane is a shelf.
    rv_editor_shelf_begin("##shelf", theme);
    if (app.preset == rv_editor_layout_preset::debug) {
        rv_editor_session_tools(app, theme, ui);
    } else if (app.scene != nullptr) {
        // Scene's tools act on a scene document; without one, none is offered.
        rv_editor_scene_tools(theme, ui);
    } else {
        rv_editor_label(rv_editor_text("pane_toolchest.no_scene"),
            rv_editor_label_kind::normal,
            theme,
            rv_editor_label_layout::wrap);
    }
    rv_editor_shelf_end();
}

} // namespace rv_editor
