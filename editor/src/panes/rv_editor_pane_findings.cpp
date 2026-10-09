// Findings: a captured frame and a written-down observation, saved as local
// files with what the editor knows about the session they came from. Test Case:
// a written check of the game and its result, saved the same way.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Multiline text input height in lines of text (multiplied by line height).
constexpr float multiline_input_height_lines = 3.5f;

// Default line number to open new test case file in editor.
constexpr int case_file_open_line = 3;

// Case file section markers and template.
constexpr std::string_view case_file_marker_title = "title:";
constexpr std::string_view case_file_marker_steps = "steps:";
constexpr std::string_view case_file_marker_expected = "expected:";
constexpr std::string_view case_file_new_template = "title: What this checks\nsteps:\n1. \nexpected:\n\n";

// Directory names.
constexpr std::string_view dir_name_testcases = "testcases";
constexpr std::string_view case_file_prefix = "case-"; // Prefix for new case filenames.

// Filename and result format markers.
constexpr std::string_view filename_timestamp_result_sep = "-";
constexpr std::string_view result_type_passed = "passed";
constexpr std::string_view result_type_failed = "failed";
constexpr std::string_view result_type_blocked = "blocked";

constexpr const char *exploratory_case_title =
    "Exploratory"; // Default test case title, written into result files - not translated.

// Result file format labels.
constexpr std::string_view result_label_test_case = "test case: ";
constexpr std::string_view result_label_result = "\nresult: ";
constexpr std::string_view result_label_note = "\nnote: ";
constexpr std::string_view result_label_recorded = "\nrecorded: ";

void rv_editor_multiline(const char *label, char *buf, size_t size)
{
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);
    ImGui::InputTextMultiline("##text",
        buf,
        size,
        ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * multiline_input_height_lines));
    ImGui::PopID();
}

// A test case file: "title: ..." on its first line, then "steps:" and "expected:"
// each on a line of its own over their text, as a finding writes them.
struct rv_editor_case {
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
    const std::string filename =
        rv_editor_now_text(time_format_file) + std::string(filename_timestamp_result_sep) + result + std::string(file_ext_text);
    const std::filesystem::path path = dir / filename;
    std::string text = std::string(result_label_test_case) + c.title + std::string(result_label_result) + result +
        std::string(result_label_note) + f.note + std::string(result_label_recorded) + rv_editor_now_text(time_format_people) +
        "\n" + rv_editor_session_text(app);
    if (rv_editor_write_file(path, text, f.error) != RV_OK) {
        return;
    }
    f.error.clear();
    f.saved.push_back(path);
    f.results.push_back(std::string(result) + ": " + c.title + ", session #" + std::to_string(app.session.number()) + ", " +
        rv_editor_now_text("%H:%M"));
    app.log.add(rv_editor_log_source::editor,
        std::string_view(result) == result_type_failed ? rv_editor_log_level::warning : rv_editor_log_level::info,
        "test case " + c.title + ": " + result + ", session #" + std::to_string(app.session.number()));
    if (std::string_view(result) == result_type_failed) {
        std::snprintf(f.title, sizeof(f.title), "%s: failed", c.title.c_str());
        std::snprintf(f.steps, sizeof(f.steps), "%s", c.steps.c_str());
        std::snprintf(f.expected, sizeof(f.expected), "%s", c.expected.c_str());
        std::snprintf(f.actual, sizeof(f.actual), "%s", f.note);
        if (app.session.live()) {
            (void)rv_editor_app_capture_frame(app);
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
    if (rv_editor_radio(rv_editor_text("findings.exploratory_desc"), f.test_case < 0, theme)) {
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
    if (rv_editor_button(rv_editor_text("findings.new_test_case"), theme)) {
        rv_editor_case_new(app);
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("findings.new_test_case_tooltip"));

    rv_editor_case c{ std::string(exploratory_case_title), {}, {} };
    if (f.test_case >= 0) {
        c = rv_editor_case_read(f.cases[static_cast<size_t>(f.test_case)]);
        ImGui::SeparatorText(rv_editor_text("findings.steps"));
        ImGui::TextWrapped("%s", c.steps.empty() ? rv_editor_text("findings.none_written") : c.steps.c_str());
        ImGui::SeparatorText(rv_editor_text("findings.expected"));
        ImGui::TextWrapped("%s", c.expected.empty() ? rv_editor_text("findings.none_written") : c.expected.c_str());
    }
    ImGui::SeparatorText(rv_editor_text("findings.result"));
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##note", f.note, sizeof(f.note), theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("findings.note_tooltip"));
    const char *why_not = app.session.number() != 0 ? nullptr : rv_editor_text("findings.no_session_yet");
    constexpr const char *results[] = { result_type_passed.data(), result_type_failed.data(), result_type_blocked.data() };
    const char *labels[] = { rv_editor_text("findings.passed"),
        rv_editor_text("findings.failed"),
        rv_editor_text("findings.blocked") };
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

void rv_editor_pane_findings(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_findings &f = app.findings;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    const std::filesystem::path dir = app.project.state_dir / std::string(dir_name_findings);
    rv_editor_shelf_begin("##shelf", theme);
    const char *why_not_capture = app.session.live() ? nullptr : rv_editor_text("findings.no_session_running");
    if (rv_editor_button(rv_editor_text("findings.capture_frame"), theme, { rv_editor_look::live, why_not_capture })) {
        (void)rv_editor_app_capture_frame(app);
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("findings.record_finding")));
    const char *why_not_record = f.title[0] == '\0' ? rv_editor_text("findings.title_first") : nullptr;
    if (rv_editor_button(rv_editor_text("findings.record_finding"), theme, { rv_editor_look::live, why_not_record })) {
        (void)rv_editor_app_save_finding(app);
    }
    rv_editor_shelf_end();

    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    if (!f.error.empty()) {
        rv_editor_status(f.error.c_str(), rv_editor_status_kind::error, theme);
    }

    ImGui::TextUnformatted(rv_editor_text("findings.title"));
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##title", f.title, sizeof(f.title), theme);
    rv_editor_multiline(rv_editor_text("findings.steps"), f.steps, sizeof(f.steps));
    rv_editor_multiline(rv_editor_text("findings.expected"), f.expected, sizeof(f.expected));
    rv_editor_multiline(rv_editor_text("findings.actual"), f.actual, sizeof(f.actual));
    rv_editor_path_row(rv_editor_text("findings.screenshot"),
        f.capture.empty() ? std::string(rv_editor_text("findings.none_screenshot")) : f.capture.string(),
        theme);
    ImGui::TextWrapped("%s", rv_editor_text("findings.session_added"));

    ImGui::SeparatorText(rv_editor_text("findings.saved_in_this_window"));
    if (f.saved.empty()) {
        const std::string dir_str = dir.string();
        const auto saved_msg = rv_editor_text_format("findings.saved_format", std::make_format_args(dir_str));
        ImGui::TextWrapped("%s", saved_msg.c_str());
    }
    for (size_t i = 0; i < f.saved.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        rv_editor_path_row(rv_editor_text("findings.finding_label"), f.saved[i].string(), theme);
        ImGui::PopID();
    }
    rv_editor_well_end();
}

} // namespace rv_editor
