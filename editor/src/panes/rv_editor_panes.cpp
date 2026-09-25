// Runtime Controls, Output and Project: views of the editor's models.

#include "panes/rv_editor_panes.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "font/rv_editor_font.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

rv_editor_status_kind rv_editor_run_lamp(const rv_editor_session &session)
{
    switch (session.state()) {
        case rv_editor_run_state::running: return rv_editor_status_kind::ok;
        case rv_editor_run_state::paused: return rv_editor_status_kind::warning;
        case rv_editor_run_state::crashed:
        case rv_editor_run_state::disconnected:
        case rv_editor_run_state::refused: return rv_editor_status_kind::error;
        case rv_editor_run_state::stopped:
        case rv_editor_run_state::exited: return rv_editor_status_kind::idle;
        default: return rv_editor_status_kind::busy;
    }
}

void rv_editor_wrapped(const std::string &text)
{
    ImGui::TextWrapped("%s", text.c_str());
}

} // namespace

void rv_editor_pane_controls(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_session &s = app.session;
    const rv_editor_transport_state state{ rv_editor_app_why_not_build(app), rv_editor_app_why_not_run(app),
        rv_editor_app_why_not_pause(app), rv_editor_app_why_not_step(app), rv_editor_app_why_not_stop(app),
        "Hot reload is not wired to the editor yet", s.state() == rv_editor_run_state::paused };
    const rv_editor_transport_actions clicked = rv_editor_transport_bar(state, theme);
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
        rv_editor_app_stop(app);
    }

    // One line: the runtime's confirmed state, and what is still pending.
    rv_editor_status(rv_editor_run_state_name(s.state()), rv_editor_run_lamp(s), theme);
    if (s.live()) {
        ImGui::SameLine();
        ImGui::Text("frame %lld, pid %d, build #%u", static_cast<long long>(s.frame()), static_cast<int>(s.pid()),
            s.build_number());
    } else if (!s.end_reason().empty()) {
        ImGui::SameLine();
        rv_editor_wrapped(s.end_reason());
    }
    if (s.uncertain()) {
        rv_editor_wrapped("A request went unanswered; the state shown is the last one the console confirmed.");
    }
    if (s.hung() || s.state() == rv_editor_run_state::disconnected) {
        if (rv_editor_button("Force Stop", theme)) {
            app.session.force_stop(app.log);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(s.hung() ? "the runtime did not end after quit" : "the channel is gone");
    }

    // The build's outcome is in the status bar and its lines in Output; here only
    // what can be done about a running one.
    if (app.build.busy()) {
        const rv_editor_state cancel = app.build.state() == rv_editor_build_state::cancelling
            ? rv_editor_state{ rv_editor_look::live, "Already cancelling" }
            : rv_editor_state{};
        if (rv_editor_button("Cancel Build", theme, cancel)) {
            app.build.cancel();
        }
    }
}

void rv_editor_pane_output(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme)
{
    rv_editor_output_view &view = app.outputs[pane];
    const rv_editor_log &log = app.log;

    for (size_t i = 0; i < view.show.size(); ++i) {
        const char *name = rv_editor_log_source_name(static_cast<rv_editor_log_source>(i));
        if (i > 0) {
            rv_editor_flow(rv_editor_checkbox_width(name));
        }
        ImGui::PushID(static_cast<int>(i));
        rv_editor_checkbox(name, &view.show[i], theme);
        ImGui::PopID();
    }
    rv_editor_flow(rv_editor_checkbox_width("follow"));
    rv_editor_checkbox("follow", &view.follow, theme);
    rv_editor_flow(rv_editor_button_width("Copy"));
    const bool copy = rv_editor_button("Copy", theme);
    rv_editor_flow(rv_editor_button_width("Clear"));
    if (rv_editor_button("Clear", theme)) {
        app.log.clear();
    }
    if (log.dropped() != 0) {
        ImGui::SameLine();
        ImGui::Text("%llu oldest lines dropped", static_cast<unsigned long long>(log.dropped()));
    }

    std::vector<const rv_editor_log_line *> shown;
    for (const rv_editor_log_line &line : log.lines()) {
        if (view.show[static_cast<size_t>(line.source)]) {
            shown.push_back(&line);
        }
    }
    auto stamp = [](const rv_editor_log_line &line) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld.%03lld", static_cast<long long>(line.ms / 60000),
            static_cast<long long>(line.ms / 1000 % 60), static_cast<long long>(line.ms % 1000));
        return std::string(buf);
    };
    if (copy) {
        std::string text;
        for (const rv_editor_log_line *line : shown) {
            text += stamp(*line) + " " + rv_editor_log_source_name(line->source) + " " + line->text + "\n";
        }
        ImGui::SetClipboardText(text.c_str());
    }

    // Process output reads in the code font (0004).
    rv_editor_font_code_push();
    rv_editor_log_begin("##lines", ImVec2(0, 0), theme);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const rv_editor_log_line &line = *shown[static_cast<size_t>(i)];
            const rv_editor_severity severity = line.level == rv_editor_log_level::error ? rv_editor_severity::error
                : line.level == rv_editor_log_level::warning                          ? rv_editor_severity::warning
                                                                                      : rv_editor_severity::info;
            rv_editor_log_row(stamp(line).c_str(), rv_editor_log_source_name(line.source), severity, line.text.c_str(),
                theme);
        }
    }
    if (view.follow && ImGui::GetScrollY() < ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    rv_editor_log_end(theme);
    rv_editor_font_code_pop();
}

void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_project &p = app.project;
    if (!p.open) {
        rv_editor_wrapped("No project is open. File > Open Folder opens a game directory, File > Open disc.toml "
                          "its manifest; both open the same workspace.");
    } else {
        rv_editor_wrapped("Root: " + p.root.string());
        rv_editor_wrapped("Disc: " + (p.disc_id.empty() ? std::string("?") : p.disc_id) +
            (p.disc_title.empty() ? "" : " - " + p.disc_title));
        if (!p.manifest_error.empty()) {
            rv_editor_wrapped("disc.toml does not parse:\n" + p.manifest_error);
        }
        rv_editor_wrapped("Builds: " + p.cache_dir.string() + "/builds");
        rv_editor_wrapped("Memory card: " + p.state_dir.string() + "/memcard.mppccard");
    }

    ImGui::Separator();
    const rv_editor_tool *tools[] = { &app.tools.console, &app.tools.burner, &app.tools.baker };
    const char *names[] = { "Runtime", "Burner", "Baker" };
    for (int i = 0; i < 3; ++i) {
        const rv_editor_tool &t = *tools[i];
        rv_editor_status(names[i], t.problem.empty() ? rv_editor_status_kind::ok : rv_editor_status_kind::error, theme);
        ImGui::SameLine();
        rv_editor_wrapped(t.problem.empty()
                ? t.path.string() + " (" + t.origin + ")" + (t.version.empty() ? "" : " - " + t.version)
                : t.problem);
    }
    const rv_editor_state look_again = app.build.busy() || app.session.live()
        ? rv_editor_state{ rv_editor_look::live, "Not while a build or the runtime is running" }
        : rv_editor_state{};
    if (rv_editor_button("Look for Tools Again", theme, look_again)) {
        rv_editor_app_init(app);
    }
    rv_editor_wrapped("Settings: " + app.tools.settings_path.string());
    if (!app.tools.settings_error.empty()) {
        rv_editor_wrapped(app.tools.settings_error);
    }
}

} // namespace rv_editor
