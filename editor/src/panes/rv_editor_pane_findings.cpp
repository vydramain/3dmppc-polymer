// Findings: a captured frame and a written-down observation, saved as local
// files with what the editor knows about the session they came from.

#include "panes/rv_editor_panes.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>

#include "imgui.h"

#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// "20260926-141203" for file names, "2026-09-26 14:12:03" for people.
std::string rv_editor_now_text(const char *format)
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), format, &tm);
    return buf;
}

std::string rv_editor_log_text(const rv_editor_log_line &line)
{
    const char *level = line.level == rv_editor_log_level::error ? "ERR"
        : line.level == rv_editor_log_level::warning             ? "WRN"
                                                                 : "INF";
    return rv_editor_log_stamp(line, true) + " [" + rv_editor_log_source_name(line.source) + "] " + level + " " + line.text;
}

// What is known about the session now, one "key: value" line each.
std::string rv_editor_session_text(const rv_editor_app &app)
{
    const rv_editor_session &s = app.session;
    const rv_editor_session_facts &f = s.facts();
    std::string out;
    out += "project: " + app.project.root.string() + "\n";
    if (s.number() == 0) {
        return out + "session: none ran in this window\n";
    }
    out += "session: #" + std::to_string(s.number()) + ", " + rv_editor_run_state_name(s.state()) + "\n";
    out += "frame: " + std::to_string(s.frame()) +
        (s.state() == rv_editor_run_state::paused ? " (paused)" : " (last reported; the machine was not paused)") + "\n";
    out += "build: #" + std::to_string(s.build_number()) + ", " + s.disc_dir().string() + "\n";
    out += "runtime: " + app.tools.console.path.string() + ", PDK " + f.pdk + "\n";
    out += "disc: " + f.disc + ", code hash " + f.code_hash + ", medium " + f.medium + "\n";
    if (f.lua_budget > 0) {
        out += "entry revision: " + std::to_string(f.revision) +
            (f.revision != f.first_revision ? " (reloaded; the build had " + std::to_string(f.first_revision) + ")"
                                            : " (as built)") +
            "\n";
    }
    if (!s.live()) {
        out += "ended: " + s.end_reason() + "\n";
    }
    return out;
}

bool rv_editor_write_file(const std::filesystem::path &path, const std::string &text, std::string &error)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

void rv_editor_capture(rv_editor_app &app, const std::filesystem::path &dir)
{
    rv_editor_findings &f = app.findings;
    const std::filesystem::path path =
        dir / (rv_editor_now_text("%Y%m%d-%H%M%S") + "-frame" + std::to_string(app.session.frame()) + ".png");
    std::string error;
    if (!rv_editor_game_capture(app, path, error)) {
        f.error = error;
        return;
    }
    f.capture = path;
    f.error.clear();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "captured " + path.string());
}

void rv_editor_record(rv_editor_app &app, const std::filesystem::path &dir)
{
    rv_editor_findings &f = app.findings;
    const std::string stem = rv_editor_now_text("%Y%m%d-%H%M%S");
    const std::filesystem::path log_path = dir / (stem + ".log");
    std::string log;
    for (const rv_editor_log_line &line : app.log.lines()) {
        if (line.seq >= app.session_first_seq && line.source != rv_editor_log_source::protocol) {
            log += rv_editor_log_text(line) + "\n";
        }
    }
    std::string text = "title: " + std::string(f.title) + "\n";
    text += "recorded: " + rv_editor_now_text("%Y-%m-%d %H:%M:%S") + "\n";
    text += rv_editor_session_text(app);
    text += "screenshot: " + (f.capture.empty() ? std::string("none") : f.capture.string()) + "\n";
    text += "log: " + log_path.string() + "\n";
    text += "\nsteps:\n" + std::string(f.steps) + "\n\nexpected:\n" + std::string(f.expected) + "\n\nactual:\n" +
        std::string(f.actual) + "\n";
    const std::filesystem::path path = dir / (stem + ".txt");
    std::string error;
    if (!rv_editor_write_file(log_path, log, error) || !rv_editor_write_file(path, text, error)) {
        f.error = error;
        return;
    }
    f.saved.push_back(path);
    f.error.clear();
    f.capture.clear();
    f.title[0] = f.steps[0] = f.expected[0] = f.actual[0] = '\0';
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "finding saved: " + path.string());
}

void rv_editor_multiline(const char *label, char *buf, size_t size)
{
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);
    ImGui::InputTextMultiline("##text", buf, size, ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 3.5f));
    ImGui::PopID();
}

} // namespace

void rv_editor_pane_findings(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_findings &f = app.findings;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    const std::filesystem::path dir = app.project.state_dir / "findings";
    // The Toolchest's Capture: done here, where the frame is saved.
    if (app.capture_request) {
        app.capture_request = false;
        if (app.session.live()) {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            rv_editor_capture(app, dir);
        }
    }

    const char *why_not_capture = app.session.live() ? nullptr : "No session is running: there is no frame to capture";
    if (rv_editor_button("Capture Frame", theme, { rv_editor_look::live, why_not_capture })) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        rv_editor_capture(app, dir);
    }
    rv_editor_flow(rv_editor_button_width("Record Finding"));
    const char *why_not_record = f.title[0] == '\0' ? "Give the finding a title first" : nullptr;
    if (rv_editor_button("Record Finding", theme, { rv_editor_look::live, why_not_record })) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        rv_editor_record(app, dir);
    }
    if (!f.error.empty()) {
        rv_editor_status(f.error.c_str(), rv_editor_status_kind::error, theme);
    }

    ImGui::TextUnformatted("Title");
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##title", f.title, sizeof(f.title), theme);
    rv_editor_multiline("Steps", f.steps, sizeof(f.steps));
    rv_editor_multiline("Expected", f.expected, sizeof(f.expected));
    rv_editor_multiline("Actual", f.actual, sizeof(f.actual));
    rv_editor_path_row("Screenshot", f.capture.empty() ? std::string("none: Capture Frame attaches one") : f.capture.string(),
        theme);
    ImGui::TextWrapped("The session, build, frame and revision are added when it is saved, with the log since the "
                       "session started.");

    ImGui::SeparatorText("Saved in this window");
    if (f.saved.empty()) {
        ImGui::TextWrapped("None yet. They go to %s.", dir.c_str());
    }
    for (size_t i = 0; i < f.saved.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        rv_editor_path_row("Finding", f.saved[i].string(), theme);
        ImGui::PopID();
    }
}

} // namespace rv_editor
