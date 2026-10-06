// Session: what ran, from which build and profile, for how long, and after it
// ends, how (a compact summary of the process, its last exchanges and its log).

#include "panes/rv_editor_panes.hpp"

#include <chrono>
#include <format>
#include <string>
#include <vector>

#include "imgui.h"

#include "platform/rv_editor_process.hpp"
#include "text/rv_editor_text.hpp"
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
        rv_editor_fact(rv_editor_text("pane_session.process"), s.console().string() + ", pid " + std::to_string(s.pid()));
        rv_editor_fact(rv_editor_text("pane_session.exit"), exit_line);
        ImGui::EndTable();
    }

    ImGui::SeparatorText(rv_editor_text("pane_session.in_flight"));
    if (s.in_flight().empty()) {
        rv_editor_dim(rv_editor_text("pane_session.in_flight_empty"));
    }
    for (const rv_editor_in_flight &req : s.in_flight()) {
        rv_editor_dim(req.overdue ? req.verb + ": no answer came, so whether it ran is unknown." :
                                    req.verb + ": still waiting when it ended.");
    }

    ImGui::SeparatorText(rv_editor_text("pane_session.last_confirmed"));
    const rv_editor_last_confirmed &c = s.last_confirmed();
    if (c.verb.empty()) {
        rv_editor_dim(rv_editor_text("pane_session.last_confirmed_empty"));
    } else {
        rv_editor_dim(c.verb + " at frame " + std::to_string(c.frame) + ", " + rv_editor_clock(c.at));
    }

    ImGui::SeparatorText(rv_editor_text("pane_session.last_stderr_lines"));
    const std::vector<const rv_editor_log_line *> err_lines = rv_editor_session_stderr(app.log, s.pid());
    if (err_lines.empty()) {
        rv_editor_dim(rv_editor_text("pane_session.last_stderr_lines_empty"));
    }
    for (const rv_editor_log_line *line : err_lines) {
        ImGui::TextUnformatted(line->text.c_str());
    }

    const std::string file = app.log.file_for(s.pid());
    if (rv_editor_button(rv_editor_text("pane_session.open_full_log"),
            theme,
            { rv_editor_look::live, file.empty() ? rv_editor_text("pane_session.open_full_log_no_file") : nullptr })) {
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
            rv_editor_dim(rv_editor_text("pane_session.no_session_yet"));
            return;
        }
        // How it ended comes first: after the game it is what is looked for.
        if (!s.live()) {
            const bool bad = s.state() == rv_editor_run_state::crashed || s.state() == rv_editor_run_state::refused;
            const auto session_number = s.number();
            const auto end_reason = s.end_reason();
            const auto status_text =
                rv_editor_text_format("pane_session.session_ended_format", std::make_format_args(session_number, end_reason));
            rv_editor_status(status_text.c_str(), bad ? rv_editor_status_kind::error : rv_editor_status_kind::ok, theme);
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
        const auto session_label = rv_editor_text("pane_session.session");
        const auto session_value = "#" + std::to_string(s.number()) + ", " + rv_editor_run_state_name(s.state());
        rv_editor_fact(session_label, session_value);
        rv_editor_fact(rv_editor_text("pane_session.build"), "#" + std::to_string(s.build_number()));
        rv_editor_fact(rv_editor_text("pane_session.profile"), app.session_profile);
        const long long minutes = seconds / seconds_per_minute;
        const long long rest_seconds = seconds % seconds_per_minute;
        const auto clock_str = rv_editor_clock(s.started_at());
        std::string time_text = s.live() ?
            rv_editor_text_format("pane_session.time_running", std::make_format_args(clock_str, minutes, rest_seconds)) :
            rv_editor_text_format("pane_session.time_finished", std::make_format_args(clock_str, minutes, rest_seconds));
        rv_editor_fact(rv_editor_text("pane_session.time"), time_text);
        const auto frame_num = std::to_string(s.frame());
        std::string frame_value;
        if (s.live()) {
            frame_value = frame_num;
        } else {
            frame_value = rv_editor_text_format("pane_session.frame_last_reported_fmt", std::make_format_args(frame_num));
        }
        rv_editor_fact(rv_editor_text("pane_session.frame"), frame_value);
        if (!f.disc.empty()) {
            const auto disc_text = rv_editor_text_format("pane_session.disc_pdk_format", std::make_format_args(f.disc, f.pdk));
            rv_editor_fact(rv_editor_text("pane_session.disc"), disc_text);
        }
        if (f.lua_budget > 0) {
            std::string entry_text;
            if (f.revision != f.first_revision) {
                entry_text = rv_editor_text_format("pane_session.entry_script_reloaded",
                    std::make_format_args(f.revision, f.first_revision));
            } else if (is_latest_build) {
                entry_text = rv_editor_text_format("pane_session.entry_script_as_built", std::make_format_args(f.revision));
            } else {
                entry_text =
                    rv_editor_text_format("pane_session.entry_script_revision_only", std::make_format_args(f.revision));
            }
            rv_editor_fact(rv_editor_text("pane_session.entry_script"), entry_text);
        }
        if (!is_latest_build) {
            const auto build_num = s.build_number();
            const auto note_text =
                rv_editor_text_format("pane_session.note_native_code_format", std::make_format_args(build_num));
            rv_editor_fact(rv_editor_text("pane_session.note"), note_text);
        }
        if (!s.reload_result().empty()) {
            const auto reload_text = s.reload_ok() ?
                rv_editor_text_format("pane_session.last_reload_applied", std::make_format_args(s.reload_result())) :
                rv_editor_text_format("pane_session.last_reload_refused", std::make_format_args(s.reload_result()));
            rv_editor_fact(rv_editor_text("pane_session.last_reload"), reload_text);
        }
        const auto saved_count = app.findings.saved.size();
        const auto saved_text = rv_editor_text_format("pane_session.saved_format", std::make_format_args(saved_count));
        rv_editor_fact(rv_editor_text("pane_session.saved"), saved_text);
        ImGui::EndTable();

        // Collapsed by default: the build path and the code hash, each with its own Copy.
        if (ImGui::CollapsingHeader(rv_editor_text("pane_session.details"))) {
            rv_editor_path_row(rv_editor_text("pane_session.build_directory"), s.disc_dir().string(), theme);
            if (!f.disc.empty()) {
                rv_editor_path_row(rv_editor_text("pane_session.code_hash"), f.code_hash, theme);
            }
        }

        ImGui::SeparatorText(rv_editor_text("pane_session.marks"));
        if (app.marks.empty()) {
            rv_editor_dim(rv_editor_text("pane_session.marks_help"));
        }
        for (size_t i = 0; i < app.marks.size(); ++i) {
            ImGui::Text("%zu. %s", i + 1, app.marks[i].c_str());
        }
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
