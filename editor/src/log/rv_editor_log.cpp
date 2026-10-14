// Output's bounded line store: keeps lines only, rv_editor_log_file writes files.

#include "log/rv_editor_log.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace rv_editor
{

namespace
{

// Milliseconds part of the log timestamp.
constexpr int ms_per_second = 1000;

// Timestamp buffer: stores formatted time strings like "HH:MM:SS.mmm" (12 chars + NUL)
constexpr size_t log_stamp_buf_bytes = 16;

// Log level codes for exported text.
constexpr const char *log_level_error_code = "ERR";
constexpr const char *log_level_warning_code = "WRN";
constexpr const char *log_level_info_code = "INF";

// The steady clock the lines count from, and the wall clock at that moment.
struct rv_editor_log_clock {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::chrono::system_clock::time_point wall = std::chrono::system_clock::now();
};

const rv_editor_log_clock &rv_editor_log_start()
{
    static const rv_editor_log_clock clock;
    return clock;
}

int64_t rv_editor_log_now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - rv_editor_log_start().start)
        .count();
}

// Severity from what the line itself says: the console's level column
// ("[ERR]"), or a compiler's "error:" / "warning:". Anything else is info.
rv_editor_log_level rv_editor_log_level_of(std::string_view text)
{
    if (text.find("[ERR]") != std::string_view::npos || text.find("[EMG]") != std::string_view::npos ||
        text.find("error:") != std::string_view::npos || text.find("error ") == 0) {
        return rv_editor_log_level::error;
    }
    if (text.find("[WRN]") != std::string_view::npos || text.find("warning:") != std::string_view::npos) {
        return rv_editor_log_level::warning;
    }
    return rv_editor_log_level::info;
}

} // namespace

std::string rv_editor_log_stamp(const rv_editor_log_line &line, bool ms)
{
    const auto wall = rv_editor_log_start().wall + std::chrono::milliseconds(line.ms);
    const std::time_t t = std::chrono::system_clock::to_time_t(wall);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[log_stamp_buf_bytes];
    const size_t n = std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    if (ms) {
        const auto ms_val = std::chrono::duration_cast<std::chrono::milliseconds>(wall.time_since_epoch()).count();
        const auto part = ms_val % ms_per_second;
        std::snprintf(buf + n, sizeof(buf) - n, ".%03lld", static_cast<long long>(part));
    }
    return buf;
}

void rv_editor_log::add(rv_editor_log_source source,
    rv_editor_log_level level,
    std::string_view text,
    rv_editor_log_channel channel,
    int64_t pid,
    uint32_t run)
{
    std::string kept(text.substr(0, line_max));
    if (text.size() > line_max) {
        kept += " [cut: " + std::to_string(text.size()) + " bytes]";
    }
    lines_.push_back({ ++seq_, rv_editor_log_now(), source, level, channel, pid, run, std::move(kept) });
    while (lines_.size() > capacity) {
        lines_.pop_front();
        ++dropped_;
    }
}

void rv_editor_log::add_stream(rv_editor_log_source source,
    std::string &partial,
    std::string_view bytes,
    rv_editor_log_channel channel,
    int64_t pid,
    uint32_t run)
{
    while (!bytes.empty()) {
        const size_t nl = bytes.find('\n');
        if (nl == std::string_view::npos) {
            // A line that never ends is still bounded: emit it in pieces.
            partial.append(bytes);
            if (partial.size() > line_max) {
                add(source, rv_editor_log_level_of(partial), partial, channel, pid, run);
                partial.clear();
            }
            return;
        }
        partial.append(bytes.substr(0, nl));
        bytes = bytes.substr(nl + 1);
        if (!partial.empty() && partial.back() == '\r') {
            partial.pop_back();
        }
        add(source, rv_editor_log_level_of(partial), partial, channel, pid, run);
        partial.clear();
    }
}

void rv_editor_log::flush_stream(rv_editor_log_source source,
    std::string &partial,
    rv_editor_log_channel channel,
    int64_t pid,
    uint32_t run)
{
    if (!partial.empty()) {
        add(source, rv_editor_log_level_of(partial), partial, channel, pid, run);
        partial.clear();
    }
}

void rv_editor_log::clear()
{
    lines_.clear();
    dropped_ = 0;
    ++seq_;
}

const char *rv_editor_log_channel_name(rv_editor_log_channel channel)
{
    switch (channel) {
    case rv_editor_log_channel::out:
        return "out";
    case rv_editor_log_channel::err:
        return "err";
    case rv_editor_log_channel::none:
        break;
    }
    return "-";
}

const char *rv_editor_log_source_name(rv_editor_log_source source)
{
    switch (source) {
    case rv_editor_log_source::editor:
        return "editor";
    case rv_editor_log_source::build:
        return "build";
    case rv_editor_log_source::candidate:
        return "candidate";
    case rv_editor_log_source::runtime:
        return "runtime";
    case rv_editor_log_source::protocol:
        return "protocol";
    case rv_editor_log_source::count:
        break;
    }
    return "?";
}

const char *rv_editor_log_level_code(rv_editor_log_level level)
{
    switch (level) {
    case rv_editor_log_level::error:
        return log_level_error_code;
    case rv_editor_log_level::warning:
        return log_level_warning_code;
    case rv_editor_log_level::info:
        break;
    }
    return log_level_info_code;
}

} // namespace rv_editor
