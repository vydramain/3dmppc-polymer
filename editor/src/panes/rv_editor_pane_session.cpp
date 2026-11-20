// Session: what ran, from which build and profile, for how long, and after it
// ends, how (a compact summary of the process, its last exchanges and its log).

#include "panes/rv_editor_panes.hpp"

#include <chrono>
#include <format>
#include <string>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_pane_say.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "platform/rv_editor_process.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_facts.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// Maximum number of stderr lines to retain in a session log.
constexpr size_t max_stderr_lines = 8;

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

// The widest row: the message box with the widest of its button rows.
float rv_editor_session_content_width()
{
    return rv_editor_message_box_min_width(
        { { rv_editor_text("panes.open_project_box") }, { rv_editor_text("pane_session.open_full_log") } });
}

// What the box says: that there was no session yet, or how the one that ended did (Open Full Log when it kept a log);
// a live session has nothing to say.
void rv_editor_session_say(rv_editor_app &app, const rv_editor_theme &theme, float width)
{
    const rv_editor_session &s = app.session;
    std::string text;
    auto kind = rv_editor_label_kind::normal;
    std::string file;
    if (s.number() == 0) {
        text = rv_editor_text("pane_session.no_session_yet");
    } else if (!s.live()) {
        const bool bad = s.state() == rv_editor_run_state::crashed || s.state() == rv_editor_run_state::refused;
        const auto session_number = s.number();
        const auto end_reason = s.end_reason();
        text = rv_editor_text_format("pane_session.session_ended_format", std::make_format_args(session_number, end_reason));
        kind = bad ? rv_editor_label_kind::error : rv_editor_label_kind::info;
        file = app.log_files.path_for(s.pid());
    }
    const std::vector<std::string> buttons =
        file.empty() ? std::vector<std::string>{} : std::vector<std::string>{ rv_editor_text("pane_session.open_full_log") };
    if (rv_editor_message_box("##say", text, kind, buttons, theme, width) == 0) {
        app.open_requests.push_back({ file, 0 });
    }
}

// The process, its exit, what was still waiting for an answer, the last confirmed reply and its recent stderr, once a
// session has ended: rows of the facts.
void rv_editor_session_summary(rv_editor_app &app, const rv_editor_session &s, const rv_editor_theme &theme)
{
    std::string exit_line = rv_editor_exit_text(s.exit_status());
    if (s.output_cut()) {
        exit_line += " (its output past this point was not kept)";
    }
    rv_editor_facts_row(rv_editor_text("pane_session.process"),
        (s.console().string() + ", pid " + std::to_string(s.pid())).c_str(),
        theme);
    rv_editor_facts_row(rv_editor_text("pane_session.exit"), exit_line.c_str(), theme);
    const char *in_flight = rv_editor_text("pane_session.in_flight");
    if (s.in_flight().empty()) {
        rv_editor_facts_row(in_flight, rv_editor_text("pane_session.in_flight_empty"), theme);
    }
    for (const rv_editor_in_flight &req : s.in_flight()) {
        rv_editor_facts_row(in_flight,
            (req.overdue ? req.verb + ": no answer came, so whether it ran is unknown." :
                           req.verb + ": still waiting when it ended.")
                .c_str(),
            theme);
    }
    const rv_editor_last_confirmed &c = s.last_confirmed();
    rv_editor_facts_row(rv_editor_text("pane_session.last_confirmed"),
        c.verb.empty() ? rv_editor_text("pane_session.last_confirmed_empty") :
                         (c.verb + " at frame " + std::to_string(c.frame) + ", " + rv_editor_clock(c.at)).c_str(),
        theme);
    const std::vector<const rv_editor_log_line *> err_lines = rv_editor_session_stderr(app.log, s.pid());
    const char *stderr_key = rv_editor_text("pane_session.last_stderr_lines");
    if (err_lines.empty()) {
        rv_editor_facts_row(stderr_key, rv_editor_text("pane_session.last_stderr_lines_empty"), theme);
    }
    for (const rv_editor_log_line *line : err_lines) {
        rv_editor_facts_row(stderr_key, line->text.c_str(), theme);
    }
}

// The session's own facts, every row there with "-" when it has no value.
void rv_editor_session_rows(rv_editor_app &app, const rv_editor_session &s, const rv_editor_theme &theme)
{
    const auto until = s.live() ? std::chrono::system_clock::now() : s.ended_at();
    const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(until - s.started_at()).count();
    const rv_editor_session_facts &f = s.facts();
    const bool is_latest_build = s.build_number() == app.build.number();
    const auto fact = [&](const char *key, const std::string &value) {
        rv_editor_facts_row(key, value.c_str(), theme);
    };
    fact(rv_editor_text("pane_session.session"), "#" + std::to_string(s.number()) + ", " + rv_editor_run_state_name(s.state()));
    fact(rv_editor_text("pane_session.build"), "#" + std::to_string(s.build_number()));
    fact(rv_editor_text("pane_session.profile"), app.session_profile);
    const long long minutes = seconds / seconds_per_minute;
    const long long rest_seconds = seconds % seconds_per_minute;
    const auto clock_str = rv_editor_clock(s.started_at());
    fact(rv_editor_text("pane_session.time"),
        s.live() ?
            rv_editor_text_format("pane_session.time_running", std::make_format_args(clock_str, minutes, rest_seconds)) :
            rv_editor_text_format("pane_session.time_finished", std::make_format_args(clock_str, minutes, rest_seconds)));
    const auto frame_num = std::to_string(s.frame());
    fact(rv_editor_text("pane_session.frame"),
        s.live() ? frame_num : rv_editor_text_format("pane_session.frame_last_reported_fmt", std::make_format_args(frame_num)));
    // The optional rows are always there ("-" when absent), so what follows them keeps its place.
    const std::string dash = rv_editor_text("pane_observe.dash");
    fact(rv_editor_text("pane_session.disc"),
        f.disc.empty() ? dash : rv_editor_text_format("pane_session.disc_pdk_format", std::make_format_args(f.disc, f.pdk)));
    std::string entry_text = dash;
    if (f.lua_budget > 0 && f.revision != f.first_revision) {
        entry_text =
            rv_editor_text_format("pane_session.entry_script_reloaded", std::make_format_args(f.revision, f.first_revision));
    } else if (f.lua_budget > 0) {
        entry_text = rv_editor_text_format(is_latest_build ? "pane_session.entry_script_as_built" :
                                                             "pane_session.entry_script_revision_only",
            std::make_format_args(f.revision));
    }
    fact(rv_editor_text("pane_session.entry_script"), entry_text);
    const auto build_num = s.build_number();
    fact(rv_editor_text("pane_session.note"),
        is_latest_build ? dash :
                          rv_editor_text_format("pane_session.note_native_code_format", std::make_format_args(build_num)));
    fact(rv_editor_text("pane_session.last_reload"),
        s.reload_result().empty() ?
            dash :
            s.reload_ok() ?
            rv_editor_text_format("pane_session.last_reload_applied", std::make_format_args(s.reload_result())) :
            rv_editor_text_format("pane_session.last_reload_refused", std::make_format_args(s.reload_result())));
    const auto saved_count = app.findings.saved.size();
    fact(rv_editor_text("pane_session.saved"),
        rv_editor_text_format("pane_session.saved_format", std::make_format_args(saved_count)));
    fact(rv_editor_text("pane_session.build_directory"), s.disc_dir().string());
    fact(rv_editor_text("pane_session.code_hash"), f.disc.empty() ? dash : f.code_hash);
}

} // namespace

rv_editor_size rv_editor_session_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_session_content_width(),
        rv_editor_tiled_box_block(),
        { rv_editor_tiled_floor(), rv_editor_tiled_floor() });
}

void rv_editor_pane_session(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_session &s = app.session;
    const float width = rv_editor_session_content_width();
    const bool shown = !rv_editor_say_no_project(app, theme, width);
    if (shown) {
        rv_editor_session_say(app, theme, width);
    }
    const bool has_session = shown && s.number() != 0;
    (void)rv_editor_tiled_area_begin("##facts_area",
        width,
        theme,
        { rv_editor_tiled_floor(), rv_editor_tiled_floor() },
        has_session);
    if (has_session) {
        if (!s.live()) {
            rv_editor_session_summary(app, s, theme);
        }
        rv_editor_session_rows(app, s, theme);
    } else if (shown) {
        rv_editor_content_empty_row(rv_editor_text("pane_session.facts_empty"), theme);
    }
    rv_editor_tiled_area_end(theme);

    (void)rv_editor_tiled_area_begin("##marks_area", width, theme);
    if (shown && app.marks.empty()) {
        rv_editor_content_empty_row(rv_editor_text("pane_session.marks_help"), theme);
    }
    for (size_t i = 0; shown && i < app.marks.size(); ++i) {
        const std::string mark_text = std::format("{}. {}", i + 1, app.marks[i]);
        ImGui::TextUnformatted(mark_text.c_str());
        rv_editor_say_whole_text();
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
