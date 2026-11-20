// Findings: a captured frame and a written-down observation, saved as local
// files with what the editor knows about the session they came from. Test Case:
// a written check of the game and its result, saved the same way. Both are built from a message strip, a form and a list
// in a content area.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_findings_result.hpp"
#include "panes/rv_editor_pane_say.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_form.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// Text lines a text area of the form shows, and the characters of the narrowest field of the form.
constexpr float text_area_lines = 2.0f;
constexpr int min_field_chars = 16;

// Result button text keys, in rv_editor_case_result_kind order.
constexpr size_t result_kind_count = static_cast<size_t>(rv_editor_case_result_kind::count);
constexpr const char *result_label_keys[] = { "findings.passed", "findings.failed", "findings.blocked" };
static_assert(std::size(result_label_keys) == result_kind_count, "result label keys must match enum count");

// The form of Findings; the value is a sample the narrowest field keeps whole.
std::vector<rv_editor_form_row_desc> rv_editor_findings_rows()
{
    static const std::string sample(min_field_chars, '0');
    return { { rv_editor_text("findings.title"), sample.c_str(), nullptr, false },
        { rv_editor_text("findings.steps"), sample.c_str(), nullptr, false },
        { rv_editor_text("findings.expected"), sample.c_str(), nullptr, false },
        { rv_editor_text("findings.actual"), sample.c_str(), nullptr, false } };
}

std::vector<std::vector<std::string>> rv_editor_findings_button_rows()
{
    return { { rv_editor_text("findings.dismiss") }, { rv_editor_text("panes.open_project_box") } };
}

float rv_editor_findings_shelf_items()
{
    return rv_editor_button_width(rv_editor_text("findings.capture_frame")) + ImGui::GetStyle().ItemSpacing.x +
        rv_editor_button_width(rv_editor_text("findings.record_finding"));
}

float rv_editor_findings_content_width()
{
    return std::max({ rv_editor_message_strip_min_width(rv_editor_findings_button_rows()),
        rv_editor_tiled_shelf_width(rv_editor_findings_shelf_items()),
        rv_editor_form_min_width(rv_editor_findings_rows()) });
}

// The Test Case row of buttons: the three results and a new case.
float rv_editor_case_buttons_width()
{
    float width = 0.0f;
    for (const char *key : { result_label_keys[0], result_label_keys[1], result_label_keys[2], "findings.new_test_case" }) {
        width += rv_editor_button_width(rv_editor_text(key)) + ImGui::GetStyle().ItemSpacing.x;
    }
    return width - ImGui::GetStyle().ItemSpacing.x;
}

// The form of Test Case: the case to run and the note to its result.
std::vector<rv_editor_form_row_desc> rv_editor_case_rows()
{
    static const std::string sample(min_field_chars, '0');
    return { { rv_editor_text("findings.case_label"), sample.c_str(), nullptr, false },
        { rv_editor_text("findings.result"), sample.c_str(), nullptr, false } };
}

float rv_editor_case_content_width()
{
    return std::max({ rv_editor_message_strip_min_width(rv_editor_findings_button_rows()),
        rv_editor_case_buttons_width(),
        rv_editor_form_min_width(rv_editor_case_rows()) });
}

// The case row: a drop-down of the free play and the listed cases, which can be long.
void rv_editor_case_pick(rv_editor_findings &f, const rv_editor_theme &theme)
{
    std::vector<const char *> items = { rv_editor_text("findings.exploratory_desc") };
    for (const rv_editor_listed_case &c : f.cases) {
        items.push_back(c.title.c_str());
    }
    int current = f.test_case + 1;
    rv_editor_form_custom_begin(rv_editor_text("findings.case_label"), theme);
    if (rv_editor_dropdown("##case", &current, items.data(), static_cast<int>(items.size()), theme)) {
        f.test_case = current - 1;
    }
    rv_editor_form_custom_end(false, nullptr, theme);
}

// What the strip says: the error with Dismiss, else `text` (the result of the last case, may be empty), or the no-project
// text with Open Project.
rv_editor_message_say rv_editor_findings_say(const rv_editor_app &app, const std::string &text)
{
    if (!app.project.open) {
        return rv_editor_say_no_project();
    }
    if (!app.findings.error.empty()) {
        return { app.findings.error, rv_editor_label_kind::error, { rv_editor_text("findings.dismiss") } };
    }
    return { text, rv_editor_label_kind::normal, {} };
}

// The strip under the area and what its button does.
void rv_editor_findings_strip(rv_editor_app &app, const rv_editor_message_say &say, float width, const rv_editor_theme &theme)
{
    const int pressed = rv_editor_message_strip("##strip", say.text, say.kind, say.buttons, theme, width);
    if (pressed != 0) {
        return;
    }
    if (!app.project.open) {
        app.open_folder_request = true;
    } else if (!rv_editor_app_request_pending(app, rv_editor_pane_request_kind::dismiss_findings_error)) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::dismiss_findings_error });
    }
}

// Prose in a content area, wrapped inside the width the bar leaves: ImGui's own wrap would follow the area's content
// width, which the wrapped text itself sets, and so never narrow again.
void rv_editor_area_wrapped(const char *text)
{
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(1.0f, ImGui::GetWindowWidth() - ImGui::GetStyle().ScrollbarSize));
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

// A row of a list whose text wraps inside the width the bar leaves, as high as the text.
void rv_editor_area_row(const char *text, const rv_editor_theme &theme)
{
    const float wrap = std::max(1.0f, ImGui::GetWindowWidth() - ImGui::GetStyle().ScrollbarSize);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    (void)rv_editor_list_row(text, "", theme, { .height = ImGui::CalcTextSize(text, nullptr, false, wrap).y });
    ImGui::GetWindowDrawList()
        ->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, ImGui::GetColorU32(ImGuiCol_Text), text, nullptr, wrap);
}

// A label in the form's column and a text field in the room right of it.
void rv_editor_findings_text(const char *label, char *buf, size_t size, bool read_only, const rv_editor_theme &theme)
{
    rv_editor_form_custom_begin(label, theme);
    (void)rv_editor_text_field((std::string("##") + label).c_str(), buf, size, theme, { .read_only = read_only });
    rv_editor_form_custom_end(false, nullptr, theme);
}

// A label in the form's column and a text area in the room right of it.
void rv_editor_findings_area_text(const char *label, char *buf, size_t size, bool read_only, const rv_editor_theme &theme)
{
    rv_editor_form_custom_begin(label, theme);
    (void)
        rv_editor_text_area((std::string("##") + label).c_str(), buf, size, text_area_lines, theme, { .read_only = read_only });
    rv_editor_form_custom_end(false, nullptr, theme);
}

// The form and, under it, the screenshot, the note on the session and the saved findings, all in the content area.
void rv_editor_findings_area(rv_editor_app &app, float strip_h, const rv_editor_theme &theme)
{
    rv_editor_findings &f = app.findings;
    (void)rv_editor_tiled_area_begin("##findings_area", rv_editor_findings_content_width(), theme, {}, false, 0.0f, strip_h);
    rv_editor_form_begin(rv_editor_findings_rows(), theme);
    const bool ro = !app.project.open;
    rv_editor_findings_text(rv_editor_text("findings.title"), f.title, sizeof(f.title), ro, theme);
    rv_editor_findings_area_text(rv_editor_text("findings.steps"), f.steps, sizeof(f.steps), ro, theme);
    rv_editor_findings_area_text(rv_editor_text("findings.expected"), f.expected, sizeof(f.expected), ro, theme);
    rv_editor_findings_area_text(rv_editor_text("findings.actual"), f.actual, sizeof(f.actual), ro, theme);
    rv_editor_form_end();
    const std::string shot = std::string(rv_editor_text("findings.screenshot")) + ": " +
        (f.capture.empty() ? std::string(rv_editor_text("findings.none_screenshot")) : f.capture.string());
    ImGui::TextUnformatted(shot.c_str());
    rv_editor_area_wrapped(rv_editor_text("findings.session_added"));
    ImGui::TextUnformatted(rv_editor_text("findings.saved_in_this_window"));
    if (f.saved.empty()) {
        const std::string dir = (app.project.state_dir / std::string(dir_name_findings)).string();
        const std::string none = rv_editor_text_format("findings.saved_format", std::make_format_args(dir));
        rv_editor_content_empty_row(none.c_str(), theme);
    }
    for (size_t i = 0; i < f.saved.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        (void)rv_editor_list_row(f.saved[i].string().c_str(), f.saved[i].string().c_str(), theme);
        ImGui::PopID();
    }
    rv_editor_tiled_area_end(theme);
}

// What the case area shows of a case's text: "(none written)" for an empty one.
void rv_editor_case_text(const char *heading, const std::string &text, bool picked)
{
    ImGui::TextUnformatted(heading);
    if (picked) {
        rv_editor_area_wrapped(text.empty() ? rv_editor_text("findings.none_written") : text.c_str());
    } else {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
    }
}

void rv_editor_case_area(rv_editor_app &app, float strip_h, const rv_editor_theme &theme)
{
    rv_editor_findings &f = app.findings;
    (void)rv_editor_tiled_area_begin("##case_area",
        rv_editor_case_content_width(),
        theme,
        { rv_editor_content_min_height() },
        false,
        0.0f,
        strip_h);
    // The chosen case is drawn from the listing's cache; the listing keeps the index in range. The headings are there
    // with no case picked, so the area never changes its bars with the state.
    const bool picked = f.test_case >= 0 && static_cast<size_t>(f.test_case) < f.cases.size();
    const rv_editor_listed_case blank;
    const rv_editor_listed_case &shown = picked ? f.cases[static_cast<size_t>(f.test_case)] : blank;
    rv_editor_case_text(rv_editor_text("findings.steps"), shown.steps, picked);
    rv_editor_case_text(rv_editor_text("findings.expected"), shown.expected, picked);
    // The latest result is the strip's text, so the list shows only the earlier ones.
    ImGui::TextUnformatted(rv_editor_text("findings.results_heading"));
    for (size_t i = 0; i + 1 < f.results.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        rv_editor_area_row(f.results[i].c_str(), theme);
        ImGui::PopID();
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace

rv_editor_size rv_editor_findings_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_findings_content_width(),
        rv_editor_tiled_shelf_block() + rv_editor_tiled_strip_block());
}

rv_editor_size rv_editor_test_case_minimum()
{
    // Two form rows and the row of buttons, each a frame high with the spacing under it.
    const float rows = 3.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
    return rv_editor_tiled_minimum(rv_editor_case_content_width(),
        rv_editor_tiled_strip_block() + rows,
        { rv_editor_content_min_height() });
}

void rv_editor_pane_test_case(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_findings &f = app.findings;
    if (app.project.open) {
        // The listing is the app's: asked for when testcases/ or a listed case changed, and not while one is queued.
        const bool listing_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cases_list);
        if (rv_editor_app_cases_stale(app) && !listing_pending) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::cases_list });
        }
    }
    rv_editor_form_begin(rv_editor_case_rows(), theme);
    rv_editor_case_pick(f, theme);
    rv_editor_form_custom_begin(rv_editor_text("findings.result"), theme);
    (void)rv_editor_text_field("##note", f.note, sizeof(f.note), theme, { .read_only = !app.project.open });
    rv_editor_item_tooltip(rv_editor_text("findings.note_tooltip"));
    if (f.note[0] == '\0' && !ImGui::IsItemActive()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + pad.x, min.y + pad.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            rv_editor_text("findings.note_placeholder"));
    }
    rv_editor_form_custom_end(false, nullptr, theme);
    rv_editor_form_end();

    const rv_editor_listed_case *picked = f.test_case >= 0 && static_cast<size_t>(f.test_case) < f.cases.size() ?
        &f.cases[static_cast<size_t>(f.test_case)] :
        nullptr;
    const char *no_session = app.session.number() != 0 ? nullptr : rv_editor_text("findings.no_session_yet");
    const bool result_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::case_result);
    const char *no_project = app.project.open ? nullptr : rv_editor_text("pane_files.no_project_open");
    const char *result_why = no_project != nullptr ? no_project :
        result_pending                             ? rv_editor_text("app_why_not.request_pending") :
                                                     no_session;
    // The request names the chosen case file; empty when exploratory.
    const std::filesystem::path case_path = picked != nullptr ? picked->path : std::filesystem::path();
    for (size_t i = 0; i < result_kind_count; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        if (rv_editor_button(rv_editor_text(result_label_keys[i]), theme, { rv_editor_look::live, result_why }) &&
            app.project.open) {
            (void)rv_editor_app_request(app,
                { rv_editor_pane_request_kind::case_result, rv_editor_tile_none, case_path, {}, i });
        }
    }
    ImGui::SameLine();
    const bool new_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::case_new);
    const char *new_why = no_project != nullptr ? no_project :
        new_pending                             ? rv_editor_text("app_why_not.request_pending") :
                                                  nullptr;
    if (rv_editor_button(rv_editor_text("findings.new_test_case"), theme, { rv_editor_look::live, new_why }) &&
        app.project.open) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::case_new });
    }
    rv_editor_item_tooltip(rv_editor_text("findings.new_test_case_tooltip"));
    const rv_editor_message_say say = rv_editor_findings_say(app, rv_editor_case_result_line(f.results));
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, rv_editor_case_content_width());
    rv_editor_case_area(app, rv_editor_message_strip_height(say.text, say.buttons, strip_w), theme);
    rv_editor_findings_strip(app, say, strip_w, theme);
}

void rv_editor_pane_findings(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_findings &f = app.findings;
    rv_editor_shelf_begin("##shelf", theme);
    const bool capture_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::capture_frame);
    const char *no_capture = app.session.live() ? nullptr : rv_editor_text("findings.no_session_running");
    const char *no_project = app.project.open ? nullptr : rv_editor_text("pane_files.no_project_open");
    const char *why_not_capture = no_project != nullptr ? no_project :
        capture_pending                                 ? rv_editor_text("app_why_not.request_pending") :
                                                          no_capture;
    if (rv_editor_button(rv_editor_text("findings.capture_frame"), theme, { rv_editor_look::live, why_not_capture }) &&
        app.project.open) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::capture_frame });
    }
    ImGui::SameLine();
    const bool save_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::save_finding);
    const char *no_title = f.title[0] == '\0' ? rv_editor_text("findings.title_first") : nullptr;
    const char *why_not_record = no_project != nullptr ? no_project :
        save_pending                                   ? rv_editor_text("app_why_not.request_pending") :
                                                         no_title;
    if (rv_editor_button(rv_editor_text("findings.record_finding"), theme, { rv_editor_look::live, why_not_record }) &&
        app.project.open) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::save_finding });
    }
    rv_editor_shelf_end();

    const rv_editor_message_say say = rv_editor_findings_say(app, std::string());
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, rv_editor_findings_content_width());
    rv_editor_findings_area(app, rv_editor_message_strip_height(say.text, say.buttons, strip_w), theme);
    rv_editor_findings_strip(app, say, strip_w, theme);
}

} // namespace rv_editor
