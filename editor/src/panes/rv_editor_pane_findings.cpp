// Findings: a captured frame and a written-down observation, saved as local
// files with what the editor knows about the session they came from. Test Case:
// a written check of the game and its result, saved the same way.

#include "panes/rv_editor_panes.hpp"

#include <string>

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

// The finding's error, with a Dismiss button that asks the app to clear it.
void rv_editor_finding_error(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_status(app.findings.error.c_str(), rv_editor_status_kind::error, theme);
    rv_editor_flow(rv_editor_button_width(rv_editor_text("findings.dismiss")));
    const bool dismiss_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::dismiss_findings_error);
    const char *dismiss_why = dismiss_pending ? rv_editor_text("app_why_not.request_pending") : nullptr;
    if (rv_editor_button(rv_editor_text("findings.dismiss"), theme, { rv_editor_look::live, dismiss_why })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::dismiss_findings_error });
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
    // The listing is the app's: asked for when testcases/ or a listed case changed, and not while one is queued.
    const bool listing_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cases_list);
    const bool listing_stale = rv_editor_app_cases_stale(app);
    if (listing_stale && !listing_pending) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::cases_list });
    }
    // No controls row at the top: the New Test Case button sits inside the
    // content between the selector and the case preview, so only a well.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    if (rv_editor_radio(rv_editor_text("findings.exploratory_desc"), f.test_case < 0, theme)) {
        f.test_case = -1;
    }
    for (size_t i = 0; i < f.cases.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (rv_editor_radio(f.cases[i].title.c_str(), f.test_case == static_cast<int32_t>(i), theme)) {
            f.test_case = static_cast<int32_t>(i);
        }
        ImGui::PopID();
    }
    const bool new_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::case_new);
    const char *new_why = new_pending ? rv_editor_text("app_why_not.request_pending") : nullptr;
    if (rv_editor_button(rv_editor_text("findings.new_test_case"), theme, { rv_editor_look::live, new_why })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::case_new });
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("findings.new_test_case_tooltip"));

    // The chosen case is drawn from the listing's cache; the listing keeps the index in range.
    const rv_editor_listed_case *picked = f.test_case >= 0 ? &f.cases[static_cast<size_t>(f.test_case)] : nullptr;
    if (picked != nullptr) {
        ImGui::SeparatorText(rv_editor_text("findings.steps"));
        ImGui::TextWrapped("%s", picked->steps.empty() ? rv_editor_text("findings.none_written") : picked->steps.c_str());
        ImGui::SeparatorText(rv_editor_text("findings.expected"));
        ImGui::TextWrapped("%s", picked->expected.empty() ? rv_editor_text("findings.none_written") : picked->expected.c_str());
    }
    ImGui::SeparatorText(rv_editor_text("findings.result"));
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##note", f.note, sizeof(f.note), theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("findings.note_tooltip"));
    const char *no_session = app.session.number() != 0 ? nullptr : rv_editor_text("findings.no_session_yet");
    const bool result_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::case_result);
    const char *result_why = result_pending ? rv_editor_text("app_why_not.request_pending") : no_session;
    // The request names the chosen case file; empty when exploratory.
    const std::filesystem::path case_path = picked != nullptr ? picked->path : std::filesystem::path();
    for (size_t i = 0; i < result_kind_count; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        if (rv_editor_button(rv_editor_text(result_label_keys[i]), theme, { rv_editor_look::live, result_why })) {
            (void)rv_editor_app_request(app,
                { rv_editor_pane_request_kind::case_result, rv_editor_tile_none, case_path, {}, i });
        }
    }
    if (!f.error.empty()) {
        rv_editor_finding_error(app, theme);
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
    const bool capture_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::capture_frame);
    const char *no_capture = app.session.live() ? nullptr : rv_editor_text("findings.no_session_running");
    const char *why_not_capture = capture_pending ? rv_editor_text("app_why_not.request_pending") : no_capture;
    if (rv_editor_button(rv_editor_text("findings.capture_frame"), theme, { rv_editor_look::live, why_not_capture })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::capture_frame });
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("findings.record_finding")));
    const bool save_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::save_finding);
    const char *no_title = f.title[0] == '\0' ? rv_editor_text("findings.title_first") : nullptr;
    const char *why_not_record = save_pending ? rv_editor_text("app_why_not.request_pending") : no_title;
    if (rv_editor_button(rv_editor_text("findings.record_finding"), theme, { rv_editor_look::live, why_not_record })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::save_finding });
    }
    rv_editor_shelf_end();

    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    if (!f.error.empty()) {
        rv_editor_finding_error(app, theme);
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
