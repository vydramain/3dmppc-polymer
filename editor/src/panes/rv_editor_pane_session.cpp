// Session: what ran, from which build and profile, for how long, and after it
// ends, how (a compact summary of the process, its last exchanges and its log).

#include "panes/rv_editor_panes.hpp"

#include <chrono>
#include <string>
#include <vector>

#include "imgui.h"

#include "platform/rv_editor_process.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Maximum number of stderr lines to retain in a session log.
constexpr size_t max_stderr_lines = 8;

// Number of columns in session information tables.
constexpr int table_column_count = 2;

// Seconds in a minute for time display conversion.
constexpr long long seconds_per_minute = 60;

// Up to the 8 most recent stderr lines this pid left in the log, oldest first.
std::vector<const rv_editor_log_line *> rv_editor_session_stderr(const rv_editor_log &log, pid_t pid)
{
    std::vector<const rv_editor_log_line *> out;
    for (const rv_editor_log_line &line : log.lines()) {
        if (line.pid != pid || line.channel != rv_editor_log_channel::err) {
            continue;
        }
        out.push_back(&line);
        if (out.size() > max_stderr_lines) {
            out.erase(out.begin());
        }
    }
    return out;
}

// The process, its exit, what was still waiting for an answer, the last
// confirmed reply and its recent stderr, once a session has ended.
void rv_editor_session_summary(rv_editor_app &app, const rv_editor_session &s, const rv_editor_theme &theme)
{
    std::string exit_line = rv_editor_exit_text(s.exit_status());
    if (s.output_cut()) {
        exit_line += " (its output past this point was not kept)";
    }
    if (ImGui::BeginTable("##session_summary", table_column_count, ImGuiTableFlags_SizingStretchProp)) {
        rv_editor_fact("Process", s.console().string() + ", pid " + std::to_string(s.pid()));
        rv_editor_fact("Exit", exit_line);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("In flight");
    if (s.in_flight().empty()) {
        rv_editor_dim("Nothing was still waiting for an answer when it ended.");
    }
    for (const rv_editor_in_flight &req : s.in_flight()) {
        rv_editor_dim(req.overdue ? req.verb + ": no answer came, so whether it ran is unknown."
                                   : req.verb + ": still waiting when it ended.");
    }

    ImGui::SeparatorText("Last confirmed");
    const rv_editor_last_confirmed &c = s.last_confirmed();
    if (c.verb.empty()) {
        rv_editor_dim("The console had confirmed nothing.");
    } else {
        rv_editor_dim(c.verb + " at frame " + std::to_string(c.frame) + ", " + rv_editor_clock(c.at));
    }

    ImGui::SeparatorText("Last stderr lines");
    const std::vector<const rv_editor_log_line *> err_lines = rv_editor_session_stderr(app.log, s.pid());
    if (err_lines.empty()) {
        rv_editor_dim("No stderr lines are left in memory for this session.");
    }
    for (const rv_editor_log_line *line : err_lines) {
        ImGui::TextUnformatted(line->text.c_str());
    }

    const std::string file = app.log.file_for(s.pid());
    if (rv_editor_button("Open Full Log", theme,
            { rv_editor_look::live, file.empty() ? "This session kept no log file." : nullptr })) {
        app.open_requests.push_back({ file, 0 });
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
    // No controls row: the facts alone, in a well; the lambda's own returns
    // reach well_end below.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        if (s.number() == 0) {
            rv_editor_dim("No session has run in this window yet: Run starts one, and this says what it was.");
            return;
        }
        // How it ended comes first: after the game it is what is looked for.
        if (!s.live()) {
            const bool bad = s.state() == rv_editor_run_state::crashed || s.state() == rv_editor_run_state::refused;
            rv_editor_status(("Session #" + std::to_string(s.number()) + " ended: " + s.end_reason()).c_str(),
                bad ? rv_editor_status_kind::error : rv_editor_status_kind::ok, theme);
        }
        if (!s.live() && s.number() > 0) {
            rv_editor_session_summary(app, s, theme);
        }
        const auto until = s.live() ? std::chrono::system_clock::now() : s.ended_at();
        const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(until - s.started_at()).count();
        const rv_editor_session_facts &f = s.facts();
        const bool is_latest_build = s.build_number() == app.build.number();
        if (!ImGui::BeginTable("##session", table_column_count, ImGuiTableFlags_SizingStretchProp)) {
            return;
        }
        rv_editor_fact("Session", "#" + std::to_string(s.number()) + ", " + rv_editor_run_state_name(s.state()));
        rv_editor_fact("Build", "#" + std::to_string(s.build_number()));
        rv_editor_fact("Profile", app.session_profile);
        const long long minutes = seconds / seconds_per_minute;
        const long long rest_seconds = seconds % seconds_per_minute;
        std::string time_text = "started " + rv_editor_clock(s.started_at()) + ", " + std::to_string(minutes) + " min ";
        time_text += std::to_string(rest_seconds) + " s" + (s.live() ? " so far" : "");
        rv_editor_fact("Time", time_text);
        rv_editor_fact("Frame", std::to_string(s.frame()) + (s.live() ? "" : ", the last reported"));
        if (!f.disc.empty()) {
            rv_editor_fact("Disc", f.disc + ", PDK " + f.pdk);
        }
        if (f.lua_budget > 0) {
            std::string entry_suffix;
            if (f.revision != f.first_revision) {
                entry_suffix = ", reloaded from " + std::to_string(f.first_revision);
            } else if (is_latest_build) {
                entry_suffix = ", as built";
            }
            rv_editor_fact("Entry script", "revision " + std::to_string(f.revision) + entry_suffix);
        }
        if (!is_latest_build) {
            rv_editor_fact("Note",
                "native code from build #" + std::to_string(s.build_number()) + "; scripts, assets, scenes from current disk");
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
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
