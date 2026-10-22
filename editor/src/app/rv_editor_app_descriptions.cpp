// The state of the component descriptions: the snapshot in the project's state_dir, and the text the log shows per state.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <format>
#include <map>
#include <string>
#include <string_view>

#include "pdk/rv_err.h"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// The build's ELF image, inside the build directory; the snapshot is taken from it.
constexpr std::string_view disc_image_name = "disc.so";
// The map's kind of a source that became code on the disc.
constexpr std::string_view code_kind = "code";
// Local time as the file log names its lines; 32 bytes hold it with its NUL.
constexpr char build_time_format[] = "%Y-%m-%d %H:%M:%S";
constexpr size_t build_time_size = 32;

std::string build_time_text(int64_t seconds)
{
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
    char buf[build_time_size] = {};
    if (localtime_r(&t, &tm) == nullptr || std::strftime(buf, sizeof(buf), build_time_format, &tm) == 0) {
        return std::to_string(seconds);
    }
    return buf;
}

// The text of the state the snapshot is in; empty when it is usable and nothing is marked.
std::string state_text(const rv_editor_app_descriptions &d)
{
    if (d.snapshot.state == rv_editor_descriptions_state::pdk_mismatch) {
        return d.snapshot.reason;
    }
    if (d.snapshot.state == rv_editor_descriptions_state::none) {
        return rv_editor_text(d.no_records ? "descriptions.no_records" : "descriptions.no_snapshot");
    }
    if (!d.build_failed) {
        return {};
    }
    const std::string built = build_time_text(d.snapshot.mark.time);
    return rv_editor_text_format("descriptions.build_failed", std::make_format_args(built));
}

// Logs the state's text once per change of state; an empty text is not logged.
void show(rv_editor_app &app)
{
    const std::string text = state_text(app.descriptions);
    if (text == app.descriptions.shown_text) {
        return;
    }
    app.descriptions.shown_text = text;
    if (text.empty()) {
        return;
    }
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, text);
}

void warn_no_state_dir(rv_editor_app &app)
{
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, rv_editor_text("descriptions.no_state_dir"));
}

void read_snapshot(rv_editor_app &app)
{
    std::string error;
    if (rv_editor_descriptions_load(app.project.state_dir, app.descriptions.snapshot, error) != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            rv_editor_text_format("descriptions.not_read", std::make_format_args(error)));
    }
}

// The map is dir + ".map", beside the build directory. It gives the code sources; no map is an empty list, not an
// error: state 3 then cannot be computed for that build.
void remember_sources(rv_editor_app &app, const std::filesystem::path &build_dir)
{
    app.descriptions.sources.clear();
    const std::map<std::string, rv_editor_map_entry> map = rv_editor_build_map_read(rv_editor_build_map_path(build_dir));
    for (const auto &[path, entry] : map) {
        if (entry.kind == code_kind) {
            app.descriptions.sources.push_back(path);
        }
    }
}

// The file is not read after a no-records build: the old file may still be on disk if its deletion failed.
void forget_snapshot(rv_editor_app &app)
{
    app.descriptions.snapshot = {};
    std::string error;
    if (rv_editor_descriptions_forget(app.project.state_dir, error) != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            rv_editor_text_format("descriptions.not_forgotten", std::make_format_args(error)));
    }
}

// Takes the snapshot from a successful build. No records (RV_ERR_NOENT) forgets it; any other failure keeps the
// previous snapshot, marked as the last build's failure.
void take_snapshot(rv_editor_app &app, const std::filesystem::path &build_dir)
{
    std::string error;
    const int capture_rv =
        rv_editor_descriptions_capture(build_dir / std::string(disc_image_name), app.project.state_dir, error);
    const bool no_records = capture_rv == RV_ERR_NOENT;
    if (capture_rv != RV_OK && !no_records) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            rv_editor_text_format("descriptions.not_taken", std::make_format_args(error)));
        app.descriptions.no_records = false;
        app.descriptions.build_failed = true;
        return;
    }
    app.descriptions.build_failed = false;
    app.descriptions.no_records = no_records;
    remember_sources(app, build_dir);
    if (no_records) {
        forget_snapshot(app);
        return;
    }
    read_snapshot(app);
}

} // namespace

void rv_editor_app_descriptions_open(rv_editor_app &app)
{
    app.descriptions = {};
    if (app.project.state_dir.empty()) {
        warn_no_state_dir(app);
    } else {
        read_snapshot(app);
    }
    show(app);
}

void rv_editor_app_descriptions_build_ended(rv_editor_app &app)
{
    // A release candidate writes an image; it is not a development build and leaves the snapshot alone.
    if (!app.build.image().empty()) {
        return;
    }
    if (app.build.dev_state() == rv_editor_build_state::failed) {
        // The no-records text names the last build; after a failure that build is not the last one.
        app.descriptions.no_records = false;
        app.descriptions.build_failed = true;
        show(app);
        return;
    }
    // A cancelled build changes nothing: the snapshot and its mark stay as they were.
    if (app.build.dev_state() != rv_editor_build_state::succeeded) {
        return;
    }
    if (app.project.state_dir.empty()) {
        warn_no_state_dir(app);
        return;
    }
    take_snapshot(app, app.build.last_success()->dir);
    show(app);
}

bool rv_editor_app_descriptions_sources_changed(const rv_editor_app &app, const std::filesystem::path &changed)
{
    const std::string rel = changed.generic_string();
    return std::find(app.descriptions.sources.begin(), app.descriptions.sources.end(), rel) != app.descriptions.sources.end();
}

} // namespace rv_editor
