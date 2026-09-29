// Session: what ran, from which build and profile, for how long, and how it
// ended; an ended session shows a short summary first.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "log/rv_editor_log.hpp"
#include "platform/rv_editor_process.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Up to 8 most recent stderr lines from this session's process, oldest first.
std::vector<std::string> rv_editor_session_stderr(const rv_editor_app &app, pid_t pid)
{
    std::vector<std::string> out;
    for (auto it = app.log.lines().rbegin(); it != app.log.lines().rend() && out.size() < 8; ++it) {
        if (it->pid == pid && it->channel == rv_editor_log_channel::err) {
            out.push_back(it->text);
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// A compact end-of-session summary, shown once a session has ended.
void rv_editor_session_summary(rv_editor_app &app, const rv_editor_session &s, const rv_editor_theme &theme)
{
    rv_editor_dim("Process: " + s.console().string() + ", pid " + std::to_string(s.pid()));
    rv_editor_dim("Exit: " + rv_editor_exit_text(s.exit_status()));

    const std::vector<rv_editor_session_pending_op> &pending = s.in_flight_at_end();
    if (pending.empty()) {
        rv_editor_dim("In flight: nothing was waiting for an answer.");
    }
    for (const rv_editor_session_pending_op &p : pending) {
        rv_editor_dim("In flight: " + p.verb +
            (p.overdue ? " (no answer came, so whether it ran is unknown)" : " (still waiting)"));
    }

    const rv_editor_session_last_op &last = s.last_confirmed();
    rv_editor_dim(last.verb.empty() ? "Last confirmed: the console confirmed nothing."
        : "Last confirmed: " + last.verb + " at frame " + std::to_string(last.frame) + ", " + rv_editor_clock(last.at));

    if (s.output_cut()) {
        rv_editor_dim("Output after exit was not fully read.");
    }

    ImGui::SeparatorText("Last stderr lines");
    const std::vector<std::string> lines = rv_editor_session_stderr(app, s.pid());
    if (lines.empty()) {
        rv_editor_dim("None are left in memory.");
    }
    for (const std::string &line : lines) {
        ImGui::TextWrapped("%s", line.c_str());
    }

    const std::filesystem::path log_file = app.log.file_of(s.pid());
    const char *no_file = "No log file was kept for this session.";
    if (rv_editor_button("Open Full Log", theme, { rv_editor_look::live, log_file.empty() ? no_file : nullptr })) {
        app.open_requests.push_back({ log_file, 0 });
    }
}

} // namespace

void rv_editor_pane_session(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_session &s = app.session;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    if (s.number() == 0) {
        rv_editor_dim("No session has run in this window yet: Run starts one, and this says what it was.");
        return;
    }
    // How it ended comes first: after the game it is what is looked for.
    if (!s.live()) {
        const bool bad = s.state() == rv_editor_run_state::crashed || s.state() == rv_editor_run_state::refused;
        rv_editor_status(("Session #" + std::to_string(s.number()) + " ended: " + s.end_reason()).c_str(),
            bad ? rv_editor_status_kind::error : rv_editor_status_kind::ok, theme);
        rv_editor_session_summary(app, s, theme);
        ImGui::SeparatorText("Facts");
    }
    const auto until = s.live() ? std::chrono::system_clock::now() : s.ended_at();
    const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(until - s.started_at()).count();
    const rv_editor_session_facts &f = s.facts();
    if (!ImGui::BeginTable("##session", 2, ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    rv_editor_fact("Session", "#" + std::to_string(s.number()) + ", " + rv_editor_run_state_name(s.state()));
    rv_editor_fact("Build", "#" + std::to_string(s.build_number()));
    rv_editor_fact("Profile", app.session_profile);
    rv_editor_fact("Time", "started " + rv_editor_clock(s.started_at()) + ", " + std::to_string(seconds / 60) + " min " +
        std::to_string(seconds % 60) + " s" + (s.live() ? " so far" : ""));
    rv_editor_fact("Frame", std::to_string(s.frame()) + (s.live() ? "" : ", the last reported"));
    if (!f.disc.empty()) {
        rv_editor_fact("Disc", f.disc + ", PDK " + f.pdk);
    }
    if (f.lua_budget > 0) {
        rv_editor_fact("Entry script", "revision " + std::to_string(f.revision) +
            (f.revision != f.first_revision ? ", reloaded from " + std::to_string(f.first_revision) : ", as built"));
    }
    if (!s.reload_result().empty()) {
        rv_editor_fact("Last reload", (s.reload_ok() ? "applied: " : "refused: ") + s.reload_result());
    }
    rv_editor_fact("Saved", std::to_string(app.findings.saved.size()) + " findings and test results in this window");
    ImGui::EndTable();

    // Collapsed by default: the build path and the code hash, each with its own Copy.
    if (ImGui::CollapsingHeader("Details")) {
        rv_editor_path_row("Build directory", s.disc_dir().string(), theme);
        if (!f.disc.empty()) {
            rv_editor_path_row("Code hash", f.code_hash, theme);
        }
    }

    ImGui::SeparatorText("Marks");
    if (app.marks.empty()) {
        rv_editor_dim("Mark Moment in the Session Toolchest notes the frame, and a word on it, while you play.");
    }
    for (size_t i = 0; i < app.marks.size(); ++i) {
        ImGui::Text("%zu. %s", i + 1, app.marks[i].c_str());
    }
}

} // namespace rv_editor
