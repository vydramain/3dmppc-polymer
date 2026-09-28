// Toolchest: Scene's tools for the scene document in front; in Debug, the
// session's: Mark Moment, Capture Frame, Report Issue and Restart.

#include "panes/rv_editor_panes.hpp"

#include <string>

#include "imgui.h"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

void rv_editor_session_tools(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_session &s = app.session;
    const char *no_session = s.live() ? nullptr : "No session is running: Run starts one";
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##mark_note", app.mark_note, sizeof(app.mark_note), theme);
    if (app.mark_note[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + pad.x, min.y + pad.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), "A word for the mark...");
    }
    if (rv_editor_command_button("##mark", 'M', theme.code_yellow, "Mark Moment",
            "Notes this frame and the word above in the session's log and in Session", theme,
            { rv_editor_look::live, no_session })) {
        const std::string mark = "frame " + std::to_string(s.frame()) +
            (app.mark_note[0] == '\0' ? std::string() : ": " + std::string(app.mark_note));
        app.marks.push_back(mark);
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
            "mark " + std::to_string(app.marks.size()) + " of session #" + std::to_string(s.number()) + ", " + mark);
        app.mark_note[0] = '\0';
    }
    if (rv_editor_command_button("##capture", 'C', theme.code_blue, "Capture Frame",
            "Saves the Game's frame into the project's findings", theme, { rv_editor_look::live, no_session })) {
        rv_editor_findings_capture(app);
    }
    // The moment is kept before the form is: a frame, the keyboard back, then Findings.
    const char *no_report = s.number() != 0 ? nullptr : "No session has run yet: there is nothing to report on";
    if (rv_editor_command_button("##report", 'I', theme.code_red, "Report Issue",
            "Captures the frame, gives the keyboard back and opens Findings, where the session, build and log "
            "are added on save",
            theme, { rv_editor_look::live, no_report })) {
        if (s.live()) {
            rv_editor_findings_capture(app);
        }
        app.game_captured = false;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
            "issue reported at frame " + std::to_string(s.frame()) + " of session #" + std::to_string(s.number()));
        app.show_request = rv_editor_pane_kind::findings;
    }
    if (rv_editor_command_button("##restart", 'R', theme.code_green, "Restart",
            "Stops the session and runs the same profile again", theme, { rv_editor_look::live, no_session })) {
        app.run_after_stop = true;
        rv_editor_app_stop(app);
    }
}

} // namespace

void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (app.preset == rv_editor_layout_preset::debug) {
        rv_editor_session_tools(app, theme);
        return;
    }
    // Scene's tools act on a scene document; without one, none is offered.
    if (app.scene != nullptr) {
        rv_editor_scene_tools(app, theme);
        return;
    }
    ImGui::TextWrapped("Select, Move, Rotate and Scale act on a scene document: Scene > New Scene or Open Scene.");
}

} // namespace rv_editor
