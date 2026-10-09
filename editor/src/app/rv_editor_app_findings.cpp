// Findings: a written-down finding saved as a text file with the session's facts and
// a log file beside it, and the text and file helpers the panes share.

#include "app/rv_editor_app.hpp"

#include <ctime>
#include <cstdint>
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

} // namespace rv_editor
