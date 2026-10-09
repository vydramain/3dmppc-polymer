// Findings: a written-down finding saved as a text file with the session's facts and
// a log file beside it; test cases (a written check and its result) saved the same way;
// and the text and file helpers the panes share.

#include "app/rv_editor_app.hpp"

#include <ctime>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <SDL3/SDL.h>

#include "pdk/rv_err.h"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Time format buffer size for timestamp "YYYYMMDD-HHMMSS".
constexpr size_t time_format_buffer_size = 32;

// Capture file name parts, and the bytes per pixel of the frame's ARGB8888 pixels.
constexpr std::string_view frame_filename_part = "-frame";
constexpr std::string_view file_ext_image = ".png";
constexpr size_t bytes_per_pixel_argb8888 = 4;
// Reads of the frame before giving up: one can overlap the console's write of that slot.
constexpr int frame_read_attempts = 3;

// File format labels and separators.
constexpr std::string_view log_text_open = " [";
constexpr std::string_view log_text_close = "] ";
constexpr std::string_view log_text_level_sep = " ";

// Findings text format: sections and labels.
constexpr std::string_view findings_label_title = "title: ";
constexpr std::string_view session_label_project = "project: "; // Label of the project string in the session text.
constexpr std::string_view findings_label_recorded = "recorded: ";
constexpr std::string_view findings_label_screenshot = "screenshot: ";
constexpr std::string_view findings_label_log = "log: ";
constexpr std::string_view findings_label_steps = "\nsteps:\n";
constexpr std::string_view findings_label_expected = "\n\nexpected:\n";
constexpr std::string_view findings_label_actual = "\n\nactual:\n";
constexpr std::string_view findings_none = "none";

// Default line number to open new test case file in editor.
constexpr int case_file_open_line = 3;

// Case file name prefix and template.
constexpr std::string_view case_file_prefix = "case-";
constexpr std::string_view case_file_new_template = "title: What this checks\nsteps:\n1. \nexpected:\n\n";

// Case file section markers: "title:" on the first line, then "steps:" and "expected:" on lines of their own.
constexpr std::string_view case_file_marker_title = "title:";
constexpr std::string_view case_file_marker_steps = "steps:";
constexpr std::string_view case_file_marker_expected = "expected:";
// The exploratory case's title; not translated, written into result files.
constexpr std::string_view case_exploratory_title = "Exploratory";

// Result file name part and labels.
constexpr std::string_view filename_timestamp_result_sep = "-";
constexpr std::string_view result_label_test_case = "test case: ";
constexpr std::string_view result_label_result = "\nresult: ";
constexpr std::string_view result_label_note = "\nnote: ";
constexpr std::string_view result_label_recorded = "\nrecorded: ";

// Mark text keys: with a note, and without one.
constexpr std::string_view mark_text_key = "pane_toolchest.mark_text";
constexpr std::string_view mark_text_key_no_note = "pane_toolchest.mark_text_no_note";

std::string rv_editor_log_text(const rv_editor_log_line &line)
{
    const char *level = rv_editor_log_level_code(line.level);
    return rv_editor_log_stamp(line, true) + std::string(log_text_open) + rv_editor_log_source_name(line.source) +
        std::string(log_text_close) + level + std::string(log_text_level_sep) + line.text;
}

// Writes the pixels (0xAARRGGBB, width x height) as a PNG. RV_OK, or RV_ERR_IO with the reason in error.
int rv_editor_capture_png(const std::filesystem::path &path,
    uint32_t width,
    uint32_t height,
    std::vector<uint32_t> &pixels,
    std::string &error)
{
    SDL_Surface *surface = SDL_CreateSurfaceFrom(static_cast<int>(width),
        static_cast<int>(height),
        SDL_PIXELFORMAT_ARGB8888,
        pixels.data(),
        static_cast<int>(width * bytes_per_pixel_argb8888));
    if (surface == nullptr) {
        error = SDL_GetError();
        return RV_ERR_IO;
    }
    const bool saved = SDL_SavePNG(surface, path.c_str());
    if (!saved) {
        error = path.string() + ": " + SDL_GetError();
    }
    SDL_DestroySurface(surface);
    if (!saved) {
        return RV_ERR_IO;
    }
    return RV_OK;
}

// The first case-N.txt in dir that does not exist yet. An empty path with the reason in error when
// the check fails: a case may already be there, so nothing is written over it.
std::filesystem::path rv_editor_case_free_path(const std::filesystem::path &dir, std::string &error)
{
    for (int n = 1;; ++n) {
        const std::filesystem::path path =
            dir / (std::string(case_file_prefix) + std::to_string(n) + std::string(file_ext_text));
        std::error_code ec;
        const bool taken = std::filesystem::exists(path, ec);
        if (ec) {
            error = "cannot check " + path.string() + ": " + ec.message();
            return {};
        }
        if (!taken) {
            return path;
        }
    }
}

// A failed case starts a finding: its title, steps and expected, with the drafted note as actual.
void rv_editor_case_start_finding(rv_editor_app &app, const rv_editor_case &c)
{
    rv_editor_findings &f = app.findings;
    std::snprintf(f.title, sizeof(f.title), "%s: failed", c.title.c_str());
    std::snprintf(f.steps, sizeof(f.steps), "%s", c.steps.c_str());
    std::snprintf(f.expected, sizeof(f.expected), "%s", c.expected.c_str());
    std::snprintf(f.actual, sizeof(f.actual), "%s", f.note);
    app.show_request = rv_editor_pane_kind::findings;
}

} // namespace

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
            (f.revision != f.first_revision ? " (reloaded; the build had " + std::to_string(f.first_revision) + ")" :
                                              " (as built)") +
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

// Saves the drafted finding and the session's log into the findings directory. RV_OK, or
// RV_ERR_IO when a file cannot be written (reason in findings.error and the log).
int rv_editor_app_save_finding(rv_editor_app &app)
{
    rv_editor_findings &f = app.findings;
    const std::filesystem::path dir = app.project.state_dir / std::string(dir_name_findings);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string error = ec ? "cannot create " + dir.string() + ": " + ec.message() : std::string();
    const std::string stem = rv_editor_now_text(time_format_file);
    const std::filesystem::path log_path = dir / (stem + std::string(file_ext_log));
    std::string log;
    for (const rv_editor_log_line &line : app.log.lines()) {
        if (line.seq >= app.session_first_seq && line.source != rv_editor_log_source::protocol) {
            log += rv_editor_log_text(line) + "\n";
        }
    }
    std::string text = std::string(findings_label_title) + std::string(f.title) + "\n";
    text += std::string(findings_label_recorded) + rv_editor_now_text(time_format_people) + "\n";
    text += rv_editor_session_text(app);
    text +=
        std::string(findings_label_screenshot) + (f.capture.empty() ? std::string(findings_none) : f.capture.string()) + "\n";
    text += std::string(findings_label_log) + log_path.string() + "\n";
    text += std::string(findings_label_steps) + std::string(f.steps) + std::string(findings_label_expected) +
        std::string(f.expected) + std::string(findings_label_actual) + std::string(f.actual) + "\n";
    const std::filesystem::path path = dir / (stem + std::string(file_ext_text));
    const int log_rv = error.empty() ? rv_editor_write_file(log_path, log, error) : RV_ERR_IO;
    const int text_rv = log_rv == RV_OK ? rv_editor_write_file(path, text, error) : log_rv;
    if (text_rv != RV_OK) {
        f.error = error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "finding not saved: " + error);
        return RV_ERR_IO;
    }
    f.saved.push_back(path);
    f.error.clear();
    f.capture.clear();
    f.title[0] = f.steps[0] = f.expected[0] = f.actual[0] = '\0';
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "finding saved: " + path.string());
    return RV_OK;
}

// Captures the newest frame of the live session; see rv_editor_app.hpp.
int rv_editor_app_capture_frame(rv_editor_app &app)
{
    rv_editor_findings &f = app.findings;
    uint64_t frame = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> pixels;
    // Only the live session's memory: a stopped session's last frame is not the game's now.
    const bool live = app.session.live();
    bool have = false;
    for (int attempt = 0; live && !have && attempt < frame_read_attempts; ++attempt) {
        have = app.session.frame_memory().read(frame, width, height, pixels);
    }
    if (!have) {
        f.error = rv_editor_text("pane_game.error_no_frame");
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "capture refused: " + f.error);
        return RV_ERR_NOENT;
    }
    const std::filesystem::path dir = app.project.state_dir / std::string(dir_name_findings);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir /
        (rv_editor_now_text(time_format_file) + std::string(frame_filename_part) + std::to_string(frame) +
            std::string(file_ext_image));
    std::string error = ec ? "cannot create " + dir.string() + ": " + ec.message() : std::string();
    const int rv = error.empty() ? rv_editor_capture_png(path, width, height, pixels, error) : RV_ERR_IO;
    if (rv != RV_OK) {
        f.error = error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "capture not saved: " + error);
        return RV_ERR_IO;
    }
    f.capture = path;
    f.error.clear();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "captured " + path.string());
    return RV_OK;
}

// A test case file: the title from its "title:" line (else the file name), then the steps and expected text.
int rv_editor_case_read(const std::filesystem::path &path, rv_editor_case &out, std::string &error)
{
    std::error_code ec;
    const bool present = std::filesystem::exists(path, ec);
    if (ec || !present) {
        error = ec ? "cannot check " + path.string() + ": " + ec.message() : "cannot read " + path.string() + ": missing";
        return RV_ERR_IO;
    }
    if (!std::filesystem::is_regular_file(path, ec)) {
        error = ec ? "cannot check " + path.string() + ": " + ec.message() : "not a regular file: " + path.string();
        return RV_ERR_IO;
    }
    std::ifstream in(path);
    if (!in) {
        error = "cannot read " + path.string();
        return RV_ERR_IO;
    }
    rv_editor_case c{ path.stem().string(), {}, {} };
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
    if (in.bad()) {
        error = "cannot read " + path.string();
        return RV_ERR_IO;
    }
    out = c;
    return RV_OK;
}

rv_editor_case rv_editor_case_exploratory()
{
    return rv_editor_case{ std::string(case_exploratory_title), {}, {} };
}

// A new case to fill in, opened in Code. RV_OK, or RV_ERR_IO with the reason in findings.error and the log.
int rv_editor_app_case_new(rv_editor_app &app)
{
    rv_editor_findings &f = app.findings;
    const std::filesystem::path dir = app.project.root / std::string(dir_name_testcases);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string error = ec ? "cannot create " + dir.string() + ": " + ec.message() : std::string();
    const std::filesystem::path path = error.empty() ? rv_editor_case_free_path(dir, error) : std::filesystem::path();
    const int rv = error.empty() ? rv_editor_write_file(path, std::string(case_file_new_template), error) : RV_ERR_IO;
    if (rv != RV_OK) {
        f.error = error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "test case not created: " + error);
        return RV_ERR_IO;
    }
    f.error.clear();
    app.open_requests.push_back({ path, case_file_open_line });
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "test case created: " + path.string());
    return RV_OK;
}

// A case result with the session, written into the findings. RV_OK, or RV_ERR_IO when it is not
// saved (reason in findings.error and the log). Failed also starts a finding and attaches the live
// session's frame; then the capture's code is returned (its reason in findings.error and the log).
int rv_editor_app_case_result(rv_editor_app &app, const rv_editor_case &c, rv_editor_case_result_kind kind)
{
    rv_editor_findings &f = app.findings;
    const std::string name(case_result_names[static_cast<size_t>(kind)]);
    const std::filesystem::path dir = app.project.state_dir / std::string(dir_name_findings);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string error = ec ? "cannot create " + dir.string() + ": " + ec.message() : std::string();
    const std::string filename =
        rv_editor_now_text(time_format_file) + std::string(filename_timestamp_result_sep) + name + std::string(file_ext_text);
    const std::filesystem::path path = dir / filename;
    const std::string text = std::string(result_label_test_case) + c.title + std::string(result_label_result) + name +
        std::string(result_label_note) + f.note + std::string(result_label_recorded) + rv_editor_now_text(time_format_people) +
        "\n" + rv_editor_session_text(app);
    const int rv = error.empty() ? rv_editor_write_file(path, text, error) : RV_ERR_IO;
    if (rv != RV_OK) {
        f.error = error;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "test case result not saved: " + error);
        return RV_ERR_IO;
    }
    const bool failed = kind == rv_editor_case_result_kind::failed;
    f.error.clear();
    f.saved.push_back(path);
    f.results.push_back(
        name + ": " + c.title + ", session #" + std::to_string(app.session.number()) + ", " + rv_editor_now_text("%H:%M"));
    const rv_editor_log_level level = failed ? rv_editor_log_level::warning : rv_editor_log_level::info;
    app.log.add(rv_editor_log_source::editor,
        level,
        "test case " + c.title + ": " + name + ", session #" + std::to_string(app.session.number()));
    if (failed) {
        rv_editor_case_start_finding(app, c);
    }
    f.note[0] = '\0';
    if (!failed || !app.session.live()) {
        return RV_OK;
    }
    return rv_editor_app_capture_frame(app);
}

// Debug's mark at the live session's frame, with the note when there is one.
int rv_editor_app_add_mark(rv_editor_app &app, const std::string &note)
{
    const rv_editor_session &s = app.session;
    if (!s.live()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "mark refused: no session is live");
        return RV_ERR_INVAL;
    }
    const int64_t frame_number = s.frame();
    const std::string mark = note.empty() ? rv_editor_text_format(mark_text_key_no_note, std::make_format_args(frame_number)) :
                                            rv_editor_text_format(mark_text_key, std::make_format_args(frame_number, note));
    app.marks.push_back(mark);
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "mark " + std::to_string(app.marks.size()) + " of session #" + std::to_string(s.number()) + ", " + mark);
    return RV_OK;
}

// Report Issue: the live session's frame is captured, Findings is asked for.
int rv_editor_app_report_problem(rv_editor_app &app)
{
    const bool ran = app.session.number() != 0;
    if (!ran) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "report refused: no session has run");
        return RV_ERR_INVAL;
    }
    if (app.session.live()) {
        // The capture logs its own failure; the report goes on without the picture.
        (void)rv_editor_app_capture_frame(app);
    }
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "issue reported at frame " + std::to_string(app.session.frame()) + " of session #" +
            std::to_string(app.session.number()));
    app.show_request = rv_editor_pane_kind::findings;
    return RV_OK;
}

int rv_editor_app_findings_dismiss_error(rv_editor_app &app)
{
    if (app.findings.error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "nothing to dismiss: no finding error");
        return RV_ERR_ALREADY;
    }
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "finding error dismissed");
    app.findings.error.clear();
    return RV_OK;
}

} // namespace rv_editor
