// Output's bounded line store.

#include "log/rv_editor_log.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace rv_editor
{

namespace
{

constexpr size_t max_sinks = 8;

// Milliseconds part of the log timestamp.
constexpr int ms_per_second = 1000;

const char *rv_editor_log_channel_name(rv_editor_log_channel channel)
{
    switch (channel) {
        case rv_editor_log_channel::out: return "out";
        case rv_editor_log_channel::err: return "err";
        case rv_editor_log_channel::none: break;
    }
    return "-";
}

// The steady clock the lines count from, and the wall clock at that moment.
struct rv_editor_log_clock
{
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
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
        rv_editor_log_start().start).count();
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
    char buf[16];
    const size_t n = std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    if (ms) {
        const auto ms_val = std::chrono::duration_cast<std::chrono::milliseconds>(wall.time_since_epoch()).count();
        const auto part = ms_val % ms_per_second;
        std::snprintf(buf + n, sizeof(buf) - n, ".%03lld", static_cast<long long>(part));
    }
    return buf;
}

void rv_editor_log::add(rv_editor_log_source source, rv_editor_log_level level, std::string_view text,
    rv_editor_log_channel channel, int64_t pid, uint32_t run)
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
    for (size_t i = 0; i < sinks_.size(); ++i) {
        if (sinks_[i].pid == pid) {
            write_to_sink(sinks_[i], lines_.back());
            break;
        }
    }
}

void rv_editor_log::write_to_sink(rv_editor_log_sink &sink, const rv_editor_log_line &line)
{
    const std::string text = rv_editor_log_stamp(line, true) + ' ' + rv_editor_log_source_name(line.source) + ' ' +
        rv_editor_log_channel_name(line.channel) + ' ' + line.text + '\n';
    errno = 0;
    const bool ok = std::fwrite(text.data(), 1, text.size(), sink.file) == text.size() && std::fflush(sink.file) == 0;
    if (ok) {
        return;
    }
    const int err = errno;
    const std::string path = sink.path;
    for (size_t i = 0; i < sinks_.size(); ++i) {
        if (&sinks_[i] == &sink) {
            drop_sink(i);
            break;
        }
    }
    add(rv_editor_log_source::editor, rv_editor_log_level::error,
        "log file write failed: " + path + ": " + std::strerror(err));
}

void rv_editor_log::drop_sink(size_t index)
{
    std::fclose(sinks_[index].file);
    sinks_.erase(sinks_.begin() + static_cast<std::ptrdiff_t>(index));
}

void rv_editor_log::attach_file(int64_t pid, const std::string &path)
{
    for (size_t i = 0; i < sinks_.size(); ++i) {
        if (sinks_[i].pid == pid) {
            drop_sink(i);
            break;
        }
    }

    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    if (ec) {
        add(rv_editor_log_source::editor, rv_editor_log_level::error,
            "log file " + path + ": " + ec.message());
        return;
    }

    errno = 0;
    FILE *file = std::fopen(path.c_str(), "w");
    if (file == nullptr) {
        const int err = errno;
        add(rv_editor_log_source::editor, rv_editor_log_level::error,
            "log file open failed: " + path + ": " + std::strerror(err));
        return;
    }

    if (sinks_.size() >= max_sinks) {
        drop_sink(0);
    }
    sinks_.push_back({ pid, path, file });
    rv_editor_log_sink &sink = sinks_.back();

    for (const rv_editor_log_line &line : lines_) {
        if (line.pid != pid) {
            continue;
        }
        write_to_sink(sink, line);
        if (file_for(pid).empty()) {
            // write_to_sink dropped the sink on failure; `sink` is now dangling.
            return;
        }
    }
}

std::string rv_editor_log::file_for(int64_t pid) const
{
    for (const rv_editor_log_sink &sink : sinks_) {
        if (sink.pid == pid) {
            return sink.path;
        }
    }
    return {};
}

rv_editor_log::~rv_editor_log()
{
    for (rv_editor_log_sink &sink : sinks_) {
        std::fclose(sink.file);
    }
}

void rv_editor_log::add_stream(rv_editor_log_source source, std::string &partial, std::string_view bytes,
    rv_editor_log_channel channel, int64_t pid, uint32_t run)
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

void rv_editor_log::flush_stream(rv_editor_log_source source, std::string &partial, rv_editor_log_channel channel,
    int64_t pid, uint32_t run)
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

const char *rv_editor_log_source_name(rv_editor_log_source source)
{
    switch (source) {
        case rv_editor_log_source::editor: return "editor";
        case rv_editor_log_source::build: return "build";
        case rv_editor_log_source::candidate: return "candidate";
        case rv_editor_log_source::runtime: return "runtime";
        case rv_editor_log_source::protocol: return "protocol";
        case rv_editor_log_source::count: break;
    }
    return "?";
}

} // namespace rv_editor
