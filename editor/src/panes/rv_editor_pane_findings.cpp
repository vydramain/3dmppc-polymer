// Findings: a captured frame and a written-down observation, saved as local
// files with what the editor knows about the session they came from. Test Case:
// a written check of the game and its result, saved the same way.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <system_error>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Time format buffer size for timestamp "YYYYMMDD-HHMMSS".
constexpr size_t time_format_buffer_size = 32;

// Multiline text input height in lines of text (multiplied by line height).
constexpr float multiline_input_height_lines = 3.5f;

// Default line number to open new test case file in editor.
constexpr int case_file_open_line = 3;

// Log level display strings for console output.
constexpr const char *log_level_error_str = "ERR";
constexpr const char *log_level_warn_str = "WRN";
constexpr const char *log_level_info_str = "INF";

// Case file section markers and template.
constexpr std::string_view case_file_marker_title = "title:";
constexpr std::string_view case_file_marker_steps = "steps:";
constexpr std::string_view case_file_marker_expected = "expected:";
constexpr std::string_view case_file_new_template = "title: What this checks\nsteps:\n1. \nexpected:\n\n";

// File format labels and separators.
constexpr std::string_view log_text_open = " [";
constexpr std::string_view log_text_close = "] ";
constexpr std::string_view log_text_level_sep = " ";
constexpr std::string_view frame_filename_part = "-frame";

// File extensions.
constexpr std::string_view file_ext_image = ".png";
constexpr std::string_view file_ext_log = ".log";
constexpr std::string_view file_ext_text = ".txt";

// Directory names.
constexpr std::string_view dir_name_testcases = "testcases";
constexpr std::string_view case_file_prefix = "case-"; // Prefix for new case filenames.
constexpr std::string_view dir_name_findings = "findings";

// Filename and result format markers.
constexpr std::string_view filename_timestamp_result_sep = "-";
constexpr std::string_view result_type_passed = "passed";
constexpr std::string_view result_type_failed = "failed";
constexpr std::string_view result_type_blocked = "blocked";

// Findings text format: sections and labels.
constexpr std::string_view findings_label_title = "title: ";
constexpr std::string_view session_label_project = "project: "; // Label of the project string in the session text.
constexpr std::string_view findings_label_recorded = "recorded: ";
constexpr std::string_view findings_label_screenshot = "screenshot: ";
constexpr std::string_view findings_label_log = "log: ";
constexpr std::string_view findings_label_steps = "\nsteps:\n";
constexpr std::string_view findings_label_expected = "\n\nexpected:\n";
constexpr std::string_view findings_label_actual = "\n\nactual:\n";

// Result file format labels.
constexpr std::string_view result_label_test_case = "test case: ";
constexpr std::string_view result_label_result = "\nresult: ";
constexpr std::string_view result_label_note = "\nnote: ";
constexpr std::string_view result_label_recorded = "\nrecorded: ";

// "20260926-141203" for file names, "2026-09-26 14:12:03" for people.
std::string rv_editor_now_text(const char *format)
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[time_format_buffer_size];
    std::strftime(buf, sizeof(buf), format, &tm);
    return buf;
}

std::string rv_editor_log_text(const rv_editor_log_line &line)
{
    const char *level = log_level_info_str;
    if (line.level == rv_editor_log_level::error) {
        level = log_level_error_str;
    } else if (line.level == rv_editor_log_level::warning) {
        level = log_level_warn_str;
    }
    return rv_editor_log_stamp(line, true) + std::string(log_text_open) + rv_editor_log_source_name(line.source) +
        std::string(log_text_close) + level + std::string(log_text_level_sep) + line.text;
}

// What is known about the session now, one "key: value" line each.
std::string rv_editor_session_text(const rv_editor_app &app)
{
    const rv_editor_session &s = app.session;
    const rv_editor_session_facts &f = s.facts();
    std::string out;
    out += std::string(session_label_project) + app.project.root.string() + "\n";
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

int rv_editor_write_file(const std::filesystem::path &path, const std::string &text, std::string &error)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) {
        error = "cannot write " + path.string();
        return RV_ERR_IO;
    }
    return RV_OK;
}

void rv_editor_capture(rv_editor_app &app, const std::filesystem::path &dir)
{
    rv_editor_findings &f = app.findings;
    const std::string filename = rv_editor_now_text("%Y%m%d-%H%M%S") + std::string(frame_filename_part) +
        std::to_string(app.session.frame()) + std::string(file_ext_image);
    const std::filesystem::path path = dir / filename;
    std::string error;
    if (rv_editor_game_capture(app, path, error) != RV_OK) {
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
    const std::filesystem::path log_path = dir / (stem + std::string(file_ext_log));
    std::string log;
    for (const rv_editor_log_line &line : app.log.lines()) {
        if (line.seq >= app.session_first_seq && line.source != rv_editor_log_source::protocol) {
            log += rv_editor_log_text(line) + "\n";
        }
    }
    std::string text = std::string(findings_label_title) + std::string(f.title) + "\n";
    text += std::string(findings_label_recorded) + rv_editor_now_text("%Y-%m-%d %H:%M:%S") + "\n";
    text += rv_editor_session_text(app);
    text += std::string(findings_label_screenshot) + (f.capture.empty() ? std::string("none") : f.capture.string()) +
        "\n";
    text += std::string(findings_label_log) + log_path.string() + "\n";
    text += std::string(findings_label_steps) + std::string(f.steps) + std::string(findings_label_expected) +
        std::string(f.expected) + std::string(findings_label_actual) + std::string(f.actual) + "\n";
    const std::filesystem::path path = dir / (stem + std::string(file_ext_text));
    std::string error;
    if (rv_editor_write_file(log_path, log, error) != RV_OK || rv_editor_write_file(path, text, error) != RV_OK) {
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
    ImGui::InputTextMultiline("##text", buf, size,
        ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * multiline_input_height_lines));
    ImGui::PopID();
}

// A test case file: "title: ..." on its first line, then "steps:" and "expected:"
// each on a line of its own over their text, as a finding writes them.
struct rv_editor_case
{
    std::string title;
    std::string steps;
    std::string expected;
};

rv_editor_case rv_editor_case_read(const std::filesystem::path &path)
{
    rv_editor_case c;
    c.title = path.stem().string();
    std::ifstream in(path);
    std::string line;
    std::string *into = nullptr;
    while (std::getline(in, line)) {
        if (line.starts_with(case_file_marker_title)) {
            const size_t pos = line.find_first_not_of(' ', case_file_marker_title.size());
            c.title = line.substr(pos == std::string::npos ? line.size() : pos);
        } else if (line == case_file_marker_steps) {
            into = &c.steps;
        } else if (line == case_file_marker_expected) {
            into = &c.expected;
        } else if (into != nullptr) {
            *into += line + "\n";
        }
    }
    return c;
}

// testcases/*.txt of the project, read again when the directory changes.
void rv_editor_cases_list(rv_editor_app &app)
{
    rv_editor_findings &f = app.findings;
    const std::filesystem::path dir = app.project.root / std::string(dir_name_testcases);
    std::error_code ec;
    const std::filesystem::file_time_type at = std::filesystem::last_write_time(dir, ec);
    if (ec || at == f.cases_read) {
        if (ec) {
            f.cases.clear();
        }
        return;
    }
    f.cases_read = at;
    f.cases.clear();
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() == file_ext_text) {
            f.cases.push_back(entry.path());
        }
    }
    std::sort(f.cases.begin(), f.cases.end());
    if (f.test_case >= static_cast<int32_t>(f.cases.size())) {
        f.test_case = -1;
    }
}

// A new case to fill in, opened in Code.
void rv_editor_case_new(rv_editor_app &app)
{
    const std::filesystem::path dir = app.project.root / std::string(dir_name_testcases);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path path;
    for (int n = 1; path.empty() || std::filesystem::exists(path, ec); ++n) {
        path = dir / (std::string(case_file_prefix) + std::to_string(n) + std::string(file_ext_text));
    }
    std::string error;
    if (rv_editor_write_file(path, std::string(case_file_new_template), error) != RV_OK) {
        app.findings.error = error;
        return;
    }
    app.open_requests.push_back({ path, case_file_open_line });
}

// Passed, Failed or Blocked, with the session; Failed also starts a finding.
void rv_editor_case_result(rv_editor_app &app, const rv_editor_case &c, const char *result)
{
    rv_editor_findings &f = app.findings;
    const std::filesystem::path dir = app.project.state_dir / std::string(dir_name_findings);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string filename = rv_editor_now_text("%Y%m%d-%H%M%S") + std::string(filename_timestamp_result_sep) +
        result + std::string(file_ext_text);
    const std::filesystem::path path = dir / filename;
    std::string text = std::string(result_label_test_case) + c.title + std::string(result_label_result) + result +
        std::string(result_label_note) + f.note + std::string(result_label_recorded) +
        rv_editor_now_text("%Y-%m-%d %H:%M:%S") + "\n" + rv_editor_session_text(app);
    if (rv_editor_write_file(path, text, f.error) != RV_OK) {
        return;
    }
    f.error.clear();
    f.saved.push_back(path);
    f.results.push_back(std::string(result) + ": " + c.title + ", session #" + std::to_string(app.session.number()) +
        ", " + rv_editor_now_text("%H:%M"));
    app.log.add(rv_editor_log_source::editor, std::string(result) == "failed" ? rv_editor_log_level::warning
                                                                             : rv_editor_log_level::info,
        "test case " + c.title + ": " + result + ", session #" + std::to_string(app.session.number()));
    if (std::string(result) == "failed") {
        std::snprintf(f.title, sizeof(f.title), "%s: failed", c.title.c_str());
        std::snprintf(f.steps, sizeof(f.steps), "%s", c.steps.c_str());
        std::snprintf(f.expected, sizeof(f.expected), "%s", c.expected.c_str());
        std::snprintf(f.actual, sizeof(f.actual), "%s", f.note);
        if (app.session.live()) {
            rv_editor_capture(app, dir);
        }
        app.show_request = rv_editor_pane_kind::findings;
    }
    f.note[0] = '\0';
}

} // namespace

void rv_editor_pane_test_case(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_findings &f = app.findings;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    rv_editor_cases_list(app);
    // No controls row at the top: the New Test Case button sits inside the
    // content between the selector and the case preview, so only a well.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    if (rv_editor_radio("Exploratory: free play, no checklist", f.test_case < 0, theme)) {
        f.test_case = -1;
    }
    // Each case file is read every frame for its title; a cache when a project keeps dozens.
    for (size_t i = 0; i < f.cases.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (rv_editor_radio(rv_editor_case_read(f.cases[i]).title.c_str(), f.test_case == static_cast<int32_t>(i), theme)) {
            f.test_case = static_cast<int32_t>(i);
        }
        ImGui::PopID();
    }
    if (rv_editor_button("New Test Case", theme)) {
        rv_editor_case_new(app);
    }
    ImGui::SetItemTooltip("Writes testcases/case-N.txt in the project and opens it in Code");

    rv_editor_case c{ "Exploratory", {}, {} };
    if (f.test_case >= 0) {
        c = rv_editor_case_read(f.cases[static_cast<size_t>(f.test_case)]);
        ImGui::SeparatorText("Steps");
        ImGui::TextWrapped("%s", c.steps.empty() ? "(none written)" : c.steps.c_str());
        ImGui::SeparatorText("Expected");
        ImGui::TextWrapped("%s", c.expected.empty() ? "(none written)" : c.expected.c_str());
    }
    ImGui::SeparatorText("Result");
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##note", f.note, sizeof(f.note), theme);
    ImGui::SetItemTooltip("What happened, in a line: kept with the result");
    const char *why_not = app.session.number() != 0 ? nullptr : "No session has run yet: play the game first";
    constexpr const char *results[] = { result_type_passed.data(), result_type_failed.data(),
        result_type_blocked.data() };
    constexpr const char *labels[] = { "Passed", "Failed", "Blocked" };
    for (size_t i = 0; i < std::size(results); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        if (rv_editor_button(labels[i], theme, { rv_editor_look::live, why_not })) {
            rv_editor_case_result(app, c, results[i]);
        }
    }
    if (!f.error.empty()) {
        rv_editor_status(f.error.c_str(), rv_editor_status_kind::error, theme);
    }
    for (const std::string &r : f.results) {
        ImGui::BulletText("%s", r.c_str());
    }
    rv_editor_well_end();
}

void rv_editor_findings_capture(rv_editor_app &app)
{
    const std::filesystem::path dir = app.project.state_dir / std::string(dir_name_findings);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    rv_editor_capture(app, dir);
}

void rv_editor_pane_findings(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_findings &f = app.findings;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    const std::filesystem::path dir = app.project.state_dir / std::string(dir_name_findings);
    rv_editor_shelf_begin("##shelf", theme);
    const char *why_not_capture = app.session.live() ? nullptr : "No session is running: there is no frame to capture";
    if (rv_editor_button("Capture Frame", theme, { rv_editor_look::live, why_not_capture })) {
        rv_editor_findings_capture(app);
    }
    rv_editor_flow(rv_editor_button_width("Record Finding"));
    const char *why_not_record = f.title[0] == '\0' ? "Give the finding a title first" : nullptr;
    if (rv_editor_button("Record Finding", theme, { rv_editor_look::live, why_not_record })) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        rv_editor_record(app, dir);
    }
    rv_editor_shelf_end();

    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
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
    rv_editor_well_end();
}

} // namespace rv_editor
