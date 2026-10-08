// Toolchest: Scene's tools for the scene document in front; in Debug, the
// session's: Mark Moment, Capture Frame, Report Issue and Restart.

#include "panes/rv_editor_panes.hpp"

#include <string>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

void rv_editor_session_tools(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_session &s = app.session;
    const char *no_session = s.live() ? nullptr : rv_editor_text("pane_toolchest.no_session");
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##mark_note", app.mark_note, sizeof(app.mark_note), theme);
    if (app.mark_note[0] == '\0' && !ImGui::IsItemActive()) {
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
            { rv_editor_look::live, no_session })) {
        const auto frame_number = s.frame();
        const std::string note(app.mark_note);
        std::string mark;
        if (app.mark_note[0] == '\0') {
            mark = rv_editor_text_format("pane_toolchest.mark_text_no_note", std::make_format_args(frame_number));
        } else {
            mark = rv_editor_text_format("pane_toolchest.mark_text", std::make_format_args(frame_number, note));
        }
        app.marks.push_back(mark);
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            "mark " + std::to_string(app.marks.size()) + " of session #" + std::to_string(s.number()) + ", " + mark);
        app.mark_note[0] = '\0';
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_toolchest.capture_button")));
    if (rv_editor_tool_button(rv_editor_text("pane_toolchest.capture_button"),
            rv_editor_glyph::capture_frame,
            theme.code_blue,
            rv_editor_text("pane_toolchest.capture_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, no_session })) {
        rv_editor_findings_capture(app);
    }
    // The moment is kept before the form is: a frame, the keyboard back, then Findings.
    const char *no_report = s.number() != 0 ? nullptr : rv_editor_text("pane_toolchest.no_report");
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_toolchest.report_button")));
    if (rv_editor_tool_button(rv_editor_text("pane_toolchest.report_button"),
            rv_editor_glyph::report_issue,
            theme.code_red,
            rv_editor_text("pane_toolchest.report_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, no_report })) {
        if (s.live()) {
            rv_editor_findings_capture(app);
        }
        app.game_captured = false;
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            "issue reported at frame " + std::to_string(s.frame()) + " of session #" + std::to_string(s.number()));
        app.show_request = rv_editor_pane_kind::findings;
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_toolchest.restart_button")));
    if (rv_editor_tool_button(rv_editor_text("pane_toolchest.restart_button"),
            rv_editor_glyph::restart,
            theme.code_green,
            rv_editor_text("pane_toolchest.restart_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, no_session })) {
        app.run_after_stop = true;
        rv_editor_app_stop(app);
    }
}

} // namespace

void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme)
{
    // Only controls, no separate content: the whole pane is a shelf.
    rv_editor_shelf_begin("##shelf", theme);
    if (app.preset == rv_editor_layout_preset::debug) {
        rv_editor_session_tools(app, theme);
    } else if (app.scene != nullptr) {
        // Scene's tools act on a scene document; without one, none is offered.
        rv_editor_scene_tools(app, theme);
    } else {
        ImGui::TextWrapped("%s", rv_editor_text("pane_toolchest.no_scene"));
    }
    rv_editor_shelf_end();
}

} // namespace rv_editor
