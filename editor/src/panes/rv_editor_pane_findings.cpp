// Findings: a captured frame and a written-down observation, saved as local
// files with what the editor knows about the session they came from. Test Case:
// a written check of the game and its result, saved the same way.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <system_error>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Multiline text input height in lines of text (multiplied by line height).
constexpr float multiline_input_height_lines = 3.5f;

// Result button text keys, in rv_editor_case_result_kind order.
constexpr size_t result_kind_count = static_cast<size_t>(rv_editor_case_result_kind::count);
constexpr const char *result_label_keys[] = { "findings.passed", "findings.failed", "findings.blocked" };
static_assert(std::size(result_label_keys) == result_kind_count, "result label keys must match enum count");

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
            rv_editor_case probe;
            std::string read_error;
            const int read_rv = rv_editor_case_read(entry.path(), probe, read_error);
            // A case that cannot be read is not listed; the log names it once, at this refresh.
            if (read_rv == RV_OK) {
                f.cases.push_back(entry.path());
            } else {
                app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "test case skipped: " + read_error);
            }
        }
    }
    std::sort(f.cases.begin(), f.cases.end());
    if (f.test_case >= static_cast<int32_t>(f.cases.size())) {
        f.test_case = -1;
    }
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
        rv_editor_case listed;
        std::string read_error;
        // A file that changed since the refresh and cannot be read now is left out of this frame.
        if (rv_editor_case_read(f.cases[i], listed, read_error) != RV_OK) {
            continue;
        }
        ImGui::PushID(static_cast<int>(i));
        if (rv_editor_radio(listed.title.c_str(), f.test_case == static_cast<int32_t>(i), theme)) {
            f.test_case = static_cast<int32_t>(i);
        }
        ImGui::PopID();
    }
    if (rv_editor_button(rv_editor_text("findings.new_test_case"), theme)) {
        (void)rv_editor_app_case_new(app);
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("findings.new_test_case_tooltip"));

    rv_editor_case c = rv_editor_case_exploratory();
    const bool chosen = f.test_case >= 0;
    std::string read_error;
    const int read_rv = chosen ? rv_editor_case_read(f.cases[static_cast<size_t>(f.test_case)], c, read_error) : RV_OK;
    if (read_rv == RV_OK && chosen) {
        ImGui::SeparatorText(rv_editor_text("findings.steps"));
        ImGui::TextWrapped("%s", c.steps.empty() ? rv_editor_text("findings.none_written") : c.steps.c_str());
        ImGui::SeparatorText(rv_editor_text("findings.expected"));
        ImGui::TextWrapped("%s", c.expected.empty() ? rv_editor_text("findings.none_written") : c.expected.c_str());
    }
    ImGui::SeparatorText(rv_editor_text("findings.result"));
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##note", f.note, sizeof(f.note), theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("findings.note_tooltip"));
    const char *no_session = app.session.number() != 0 ? nullptr : rv_editor_text("findings.no_session_yet");
    // A chosen case that cannot be read has no result to record: why the buttons are off is the read's error.
    const char *why_not = read_rv == RV_OK ? no_session : read_error.c_str();
    for (size_t i = 0; i < result_kind_count; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        if (rv_editor_button(rv_editor_text(result_label_keys[i]), theme, { rv_editor_look::live, why_not })) {
            (void)rv_editor_app_case_result(app, c, static_cast<rv_editor_case_result_kind>(i));
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
