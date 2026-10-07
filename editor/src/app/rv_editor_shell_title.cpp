// The pane titles: what each header says about its pane's subject.

#include <string>

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_panes.hpp"
#include "session/rv_editor_session.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Title text with optional subject suffix. If suffix_value is empty, returns base;
// otherwise formats "base + subject_suffix(suffix_value)".
std::string
title_subject(const std::string &text_id_normal, const std::string &text_id_burn, const std::string &suffix_value, bool burn)
{
    const auto text_id = burn ? text_id_burn : text_id_normal;
    const std::string base = rv_editor_text(text_id);
    if (suffix_value.empty()) {
        return base;
    }
    const auto sfx = rv_editor_text_format("shell_titles.subject_suffix", std::make_format_args(suffix_value));
    return base + sfx;
}

// Scene title: name, modified marker, read-only status.
std::string title_scene(rv_editor_app &app)
{
    if (app.scene == nullptr) {
        return "";
    }
    const std::string scene_name = rv_editor_app_scene_name(app);
    const std::string modified = app.scene->dirty ? rv_editor_text("shell_titles.modified_marker") : "";
    const std::string readonly = app.scene->scene.read_only.empty() ? "" : rv_editor_text("shell_titles.scene_readonly");
    return std::string(rv_editor_text("shell_titles.scene_prefix")) + scene_name + modified + readonly;
}

// Problem count in the Problems pane.
std::string title_problems(rv_editor_app &app)
{
    if (app.problems.empty()) {
        return "";
    }
    const auto count = app.problems.size();
    return rv_editor_text_format("shell_titles.problems", std::make_format_args(count));
}

// Build log with last candidate: when candidates exist, format with its number; no candidates -> reason text.
std::string title_candidate_log(rv_editor_app &app)
{
    if (app.release.candidates.empty()) {
        return rv_editor_text("shell_titles.build_log_no_candidate");
    }
    const auto num = app.release.candidates.back().number;
    return rv_editor_text_format("shell_titles.build_log_candidate", std::make_format_args(num));
}

// Build log title: build number or candidate when viewing candidate log.
std::string title_build_log(rv_editor_app &app, rv_editor_pane_id pane)
{
    const auto it = app.outputs.find(pane);
    const bool candidate_view =
        it != app.outputs.end() && it->second.show[static_cast<size_t>(rv_editor_log_source::candidate)];
    if (candidate_view) {
        return title_candidate_log(app);
    }
    if (app.build.number() != 0) {
        const auto build_num = app.build.number();
        return rv_editor_text_format("shell_titles.build_log_build", std::make_format_args(build_num));
    }
    return "";
}

// Terminal title: current working directory when running; exit code when ended.
std::string title_terminal(rv_editor_app &app, rv_editor_pane_id pane)
{
    const auto it = app.terminals.find(pane);
    if (it == app.terminals.end() || it->second.term == nullptr) {
        return "";
    }
    if (it->second.term->running()) {
        return std::string(rv_editor_text("shell_titles.terminal_prefix")) + it->second.cwd.filename().string();
    }
    const auto ended_msg = it->second.term->ended();
    return rv_editor_text_format("shell_titles.terminal_ended", std::make_format_args(ended_msg));
}

// Code pane title: buffer name and modified marker.
std::string title_code(rv_editor_app &app, rv_editor_pane_id pane)
{
    if (!app.nvim.running()) {
        return "";
    }
    const rv_editor_nvim_buffer *buf = app.nvim.buffer_in(app.nvim.window_for(pane));
    if (buf == nullptr) {
        return "";
    }
    const std::string label = rv_editor_shell_buffer_label(app, buf->name);
    const std::string modified = buf->modified ? rv_editor_text("shell_titles.modified_marker") : "";
    return std::string(rv_editor_text("shell_titles.code_prefix")) + label + modified;
}

// Toolchest title based on current layout preset.
std::string title_toolchest(rv_editor_app &app)
{
    return rv_editor_text(
        app.preset == rv_editor_layout_preset::debug ? "shell_titles.session_toolchest" : "shell_titles.transform_toolchest");
}

} // namespace

void rv_editor_shell_title(rv_editor_shell &shell, rv_editor_pane_id pane, rv_editor_pane_kind kind)
{
    rv_editor_app &app = shell.app;
    const bool burn = shell.active == rv_editor_layout_preset::burn;
    // Debug's Session pane already spells out session/state; these titles stay plain there.
    const bool debug = shell.active == rv_editor_layout_preset::debug;
    const rv_editor_session &s = app.session;
    const std::string session = s.number() == 0 || debug ?
        std::string() :
        "session " + std::to_string(s.number()) + " (" + rv_editor_run_state_name(s.state()) + ")";
    const std::string candidate = app.release.candidates.empty() ?
        std::string() :
        "#" + std::to_string(app.release.candidates[app.release.selected].number);

    std::string title;
    switch (kind) {
    case rv_editor_pane_kind::game:
        title = title_subject("workspace.pane_game", "shell_titles.candidate_playtest", burn ? candidate : session, burn);
        break;
    case rv_editor_pane_kind::runtime_log:
        title = title_subject("shell_titles.runtime_log", "shell_titles.playtest_log", burn ? candidate : session, burn);
        break;
    case rv_editor_pane_kind::scene:
        title = title_scene(app);
        break;
    case rv_editor_pane_kind::problems:
        title = title_problems(app);
        break;
    case rv_editor_pane_kind::output:
        title = rv_editor_output_title(app, pane);
        break;
    case rv_editor_pane_kind::build_log:
        title = title_build_log(app, pane);
        break;
    case rv_editor_pane_kind::terminal:
        title = title_terminal(app, pane);
        break;
    case rv_editor_pane_kind::code:
        title = title_code(app, pane);
        break;
    case rv_editor_pane_kind::toolchest:
        title = title_toolchest(app);
        break;
    default:
        break;
    }

    if (!title.empty()) {
        shell.ws.titles[pane] = title;
    }
}

} // namespace rv_editor
