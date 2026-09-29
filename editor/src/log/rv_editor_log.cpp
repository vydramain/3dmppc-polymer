// Output's bounded line store.

#include "log/rv_editor_log.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <system_error>

namespace rv_editor
{

namespace
{

// How many sinks attach_file keeps open at once (see the ponytail note on
// rv_editor_log::sinks_).
constexpr size_t rv_editor_log_sink_max = 8;

const char *rv_editor_log_channel_letter(rv_editor_log_channel channel)
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
        const auto part = std::chrono::duration_cast<std::chrono::milliseconds>(wall.time_since_epoch()).count() % 1000;
        std::snprintf(buf + n, sizeof(buf) - n, ".%03lld", static_cast<long long>(part));
    }
    return buf;
}

void rv_editor_log::add(rv_editor_log_source source, rv_editor_log_level level, std::string_view text)
{
    add(source, level, text, rv_editor_log_origin{});
}

void rv_editor_log::add(rv_editor_log_source source, rv_editor_log_level level, std::string_view text,
    const rv_editor_log_origin &origin)
{
    std::string kept(text.substr(0, line_max));
    if (text.size() > line_max) {
        kept += " [cut: " + std::to_string(text.size()) + " bytes]";
    }
    lines_.push_back({ ++seq_, rv_editor_log_now(), source, level, std::move(kept), origin.channel, origin.pid,
        origin.run });
    if (origin.pid != 0) {
        for (size_t i = 0; i < sinks_.size(); ++i) {
            if (sinks_[i].pid != origin.pid) {
                continue;
            }
            if (!write_sink(sinks_[i], lines_.back())) {
                const int err = errno;
                const std::filesystem::path path = sinks_[i].path;
                close_sink(sinks_[i]);
                sinks_.erase(sinks_.begin() + static_cast<std::ptrdiff_t>(i));
                add(rv_editor_log_source::editor, rv_editor_log_level::error,
                    path.string() + ": " + std::strerror(err));
            }
            break;
        }
    }
    while (lines_.size() > capacity) {
        lines_.pop_front();
        ++dropped_;
    }
}

void rv_editor_log::add_stream(rv_editor_log_source source, std::string &partial, std::string_view bytes)
{
    add_stream(source, partial, bytes, rv_editor_log_origin{});
}

void rv_editor_log::add_stream(rv_editor_log_source source, std::string &partial, std::string_view bytes,
    const rv_editor_log_origin &origin)
{
    while (!bytes.empty()) {
        const size_t nl = bytes.find('\n');
        if (nl == std::string_view::npos) {
            // A line that never ends is still bounded: emit it in pieces.
            partial.append(bytes);
            if (partial.size() > line_max) {
                add(source, rv_editor_log_level_of(partial), partial, origin);
                partial.clear();
            }
            return;
        }
        partial.append(bytes.substr(0, nl));
        bytes = bytes.substr(nl + 1);
        if (!partial.empty() && partial.back() == '\r') {
            partial.pop_back();
        }
        add(source, rv_editor_log_level_of(partial), partial, origin);
        partial.clear();
    }
}

void rv_editor_log::flush_stream(rv_editor_log_source source, std::string &partial)
{
    flush_stream(source, partial, rv_editor_log_origin{});
}

void rv_editor_log::flush_stream(rv_editor_log_source source, std::string &partial,
    const rv_editor_log_origin &origin)
{
    if (!partial.empty()) {
        add(source, rv_editor_log_level_of(partial), partial, origin);
        partial.clear();
    }
}

void rv_editor_log::clear()
{
    lines_.clear();
    dropped_ = 0;
    ++seq_;
}

bool rv_editor_log::write_sink(rv_editor_log_sink &sink, const rv_editor_log_line &line) const
{
    if (sink.file == nullptr) {
        return true;
    }
    const int n = std::fprintf(sink.file, "%s %s %s %s\n", rv_editor_log_stamp(line, true).c_str(),
        rv_editor_log_source_name(line.source), rv_editor_log_channel_letter(line.channel), line.text.c_str());
    return n >= 0 && std::fflush(sink.file) == 0;
}

void rv_editor_log::close_sink(rv_editor_log_sink &sink)
{
    if (sink.file != nullptr) {
        std::fclose(sink.file);
        sink.file = nullptr;
    }
}

std::filesystem::path rv_editor_log::file_of(pid_t pid) const
{
    for (const rv_editor_log_sink &sink : sinks_) {
        if (sink.pid == pid) {
            return sink.path;
        }
    }
    return {};
}

void rv_editor_log::attach_file(pid_t pid, const std::filesystem::path &path)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::FILE *file = std::fopen(path.string().c_str(), "w");
    if (file == nullptr) {
        add(rv_editor_log_source::editor, rv_editor_log_level::error, path.string() + ": " + std::strerror(errno));
        return;
    }
    if (sinks_.size() >= rv_editor_log_sink_max) {
        close_sink(sinks_.front());
        sinks_.erase(sinks_.begin());
    }
    rv_editor_log_sink sink{ pid, path, file };
    for (const rv_editor_log_line &line : lines_) {
        if (line.pid != pid) {
            continue;
        }
        if (!write_sink(sink, line)) {
            const int err = errno;
            close_sink(sink);
            add(rv_editor_log_source::editor, rv_editor_log_level::error,
                path.string() + ": " + std::strerror(err));
            return;
        }
    }
    sinks_.push_back(std::move(sink));
}

rv_editor_log::~rv_editor_log()
{
    for (rv_editor_log_sink &sink : sinks_) {
        close_sink(sink);
    }
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
