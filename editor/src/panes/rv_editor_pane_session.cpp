// Session: what ran, from which build and profile, for how long, and after it
// ends, how (a compact summary of the process, its last exchanges and its log).

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_mark_tools.hpp"
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

// "HH:MM:SS" is the first 8 characters of a clock.
constexpr size_t clock_chars = 8;

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

// The Notes title row's controls: the note field and the three buttons.
float rv_editor_session_controls_width()
{
    return rv_editor_mark_tools_min_width(rv_editor_theme_olive, false);
}

// The widest row: the strip, or the Notes title with its controls.
float rv_editor_session_content_width()
{
    // The area's frame on both sides and its vertical bar.
    const rv_editor_theme &theme = rv_editor_theme_olive;
    return std::max(rv_editor_message_strip_min_width({ { rv_editor_text("panes.open_project_box") } }),
        rv_editor_content_section_min_width(rv_editor_text("pane_session.notes_title"), rv_editor_session_controls_width()) +
            2.0f * static_cast<float>(theme.pad_px * theme.scale) + ImGui::GetStyle().ScrollbarSize);
}

// The process, its exit, what was still waiting for an answer and the last confirmed reply, once a session has ended:
// rows of the facts.
void rv_editor_session_summary(const rv_editor_session &s, const rv_editor_theme &theme)
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
    return rv_editor_tiled_minimum(rv_editor_session_content_width(), 0.0f, { rv_editor_tiled_floor() });
}

std::vector<rv_editor_note> rv_editor_session_notes(const rv_editor_app &app)
{
    const rv_editor_session &s = app.session;
    if (s.number() == 0) {
        return {};
    }
    const auto at = [](std::chrono::system_clock::time_point t) {
        return rv_editor_clock(t).substr(0, clock_chars);
    };
    const auto number = s.number();
    const auto build = s.build_number();
    std::vector<std::pair<std::chrono::system_clock::time_point, std::string>> events{ { s.started_at(),
        rv_editor_text_format("pane_session.note_started_format",
            std::make_format_args(number, build, app.session_profile)) } };
    for (const rv_editor_mark &m : app.marks) {
        events.emplace_back(m.at, rv_editor_text_format("pane_session.note_mark_format", std::make_format_args(m.text)));
    }
    for (const rv_editor_log_line *line : rv_editor_session_stderr(app.log, s.pid())) {
        events.emplace_back(rv_editor_log_time(*line), line->text);
    }
    if (!s.live()) {
        std::string reason = s.end_reason();
        const std::string exit = rv_editor_exit_text(s.exit_status());
        if (reason.find(exit) == std::string::npos) {
            reason += " (" + exit + ")";
        }
        events.emplace_back(s.ended_at(),
            rv_editor_text_format("pane_session.note_ended_format", std::make_format_args(number, reason)));
    }
    // By time, so a mark stands between the lines around it; equal times keep start, marks, stderr, end.
    std::ranges::stable_sort(events, {}, &decltype(events)::value_type::first);
    std::vector<rv_editor_note> notes;
    for (auto &[time, text] : events) {
        notes.push_back({ at(time), std::move(text) });
    }
    return notes;
}

void rv_editor_pane_session(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    const rv_editor_session &s = app.session;
    const float width = rv_editor_session_content_width();
    if (!app.project.open) {
        // Nothing to show without a project: the strip that says so.
        const rv_editor_message_say none = rv_editor_say_no_project();
        if (rv_editor_message_strip("##strip",
                none.text,
                none.kind,
                none.buttons,
                theme,
                std::max(ImGui::GetContentRegionAvail().x, width)) == 0) {
            app.open_folder_request = true;
        }
        return;
    }
    const bool has_session = s.number() != 0;
    (void)rv_editor_tiled_area_begin("##area", width, theme, { rv_editor_tiled_floor() }, has_session);
    if (has_session) {
        if (!s.live()) {
            rv_editor_session_summary(s, theme);
        }
        rv_editor_session_rows(app, s, theme);
        rv_editor_facts_table_end();
    }
    rv_editor_content_section_begin(rv_editor_text("pane_session.notes_title"), rv_editor_session_controls_width(), theme);
    (void)rv_editor_mark_tools(app, theme, ui, false);
    rv_editor_content_section_end(theme);
    const std::string file = s.number() != 0 && !s.live() ? app.log_files.path_for(s.pid()) : std::string();
    const std::vector<rv_editor_note> notes = rv_editor_session_notes(app);
    if (notes.empty()) {
        rv_editor_content_empty_row(rv_editor_text("pane_session.no_session_yet"), theme);
    }
    for (const rv_editor_note &note : notes) {
        const std::string line = note.time + " " + note.text;
        ImGui::TextUnformatted(line.c_str());
        rv_editor_say_whole_text();
    }
    if (!file.empty()) {
        if (ImGui::Button(rv_editor_text("pane_session.open_full_log"))) {
            app.open_requests.push_back({ file, 0 });
        }
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
