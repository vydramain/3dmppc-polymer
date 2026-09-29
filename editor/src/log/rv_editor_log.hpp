#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace rv_editor
{

// Where an Output line came from.
enum class rv_editor_log_source : uint8_t
{
    editor,
    build,
    candidate, // release-candidate builds, separate from ordinary dev builds
    runtime,
    protocol, // the dev channel's own traffic, hidden by default (TRM-02)
    count,
};

enum class rv_editor_log_level : uint8_t
{
    info,
    warning,
    error,
};

// A process line's stream: none for the editor's own lines, out/err for a
// child process's stdout/stderr.
enum class rv_editor_log_channel : uint8_t
{
    none,
    out,
    err,
};

// Who a line came from: which stream, which process (0 = the editor itself),
// and which run of that process (0 = outside any run).
struct rv_editor_log_origin
{
    rv_editor_log_channel channel = rv_editor_log_channel::none;
    pid_t pid = 0;
    uint32_t run = 0;
};

struct rv_editor_log_line
{
    uint64_t seq;
    int64_t ms;             // milliseconds since the editor started
    rv_editor_log_source source;
    rv_editor_log_level level;
    std::string text;
    rv_editor_log_channel channel = rv_editor_log_channel::none;
    pid_t pid = 0;
    uint32_t run = 0;
};

// Output's model: every process line the editor keeps, oldest first. Bounded
// (NFR-04): past `capacity` the oldest lines go and `dropped` counts them. It
// lives outside the UI, so closing an Output pane loses nothing (LAY-09).
class rv_editor_log
{
public:
    static constexpr size_t capacity = 20000;
    // One line longer than this is cut, and says so.
    static constexpr size_t line_max = 4096;

    void add(rv_editor_log_source source, rv_editor_log_level level, std::string_view text);
    void add(rv_editor_log_source source, rv_editor_log_level level, std::string_view text,
        const rv_editor_log_origin &origin);

    // Splits `bytes` from a process stream into lines; a trailing partial line
    // waits in `partial` for the next chunk. Severity comes from the text.
    void add_stream(rv_editor_log_source source, std::string &partial, std::string_view bytes);
    void add_stream(rv_editor_log_source source, std::string &partial, std::string_view bytes,
        const rv_editor_log_origin &origin);
    // Whatever `partial` still holds, as a last line (the stream has ended).
    void flush_stream(rv_editor_log_source source, std::string &partial);
    void flush_stream(rv_editor_log_source source, std::string &partial, const rv_editor_log_origin &origin);

    void clear();

    const std::deque<rv_editor_log_line> &lines() const { return lines_; }
    uint64_t dropped() const { return dropped_; }
    // Changes every time a line is added or the log is cleared.
    uint64_t revision() const { return seq_; }

    // Writes every kept line with origin.pid == pid to `path` (its parent
    // directory is created), then appends each later line with that pid as it
    // is added. On failure logs one editor error line naming `path` and the OS
    // reason, and attaches nothing; never throws.
    void attach_file(pid_t pid, const std::filesystem::path &path);
    // The path attach_file opened for `pid`, or empty when none is open.
    std::filesystem::path file_of(pid_t pid) const;

    rv_editor_log() = default;
    // Owns FILE* sinks: copying would double-close them.
    rv_editor_log(const rv_editor_log &) = delete;
    rv_editor_log &operator=(const rv_editor_log &) = delete;
    ~rv_editor_log();

private:
    struct rv_editor_log_sink
    {
        pid_t pid = 0;
        std::filesystem::path path;
        std::FILE *file = nullptr;
    };

    // False on a write/flush failure; the caller closes and drops that sink.
    bool write_sink(rv_editor_log_sink &sink, const rv_editor_log_line &line) const;
    static void close_sink(rv_editor_log_sink &sink);

    std::deque<rv_editor_log_line> lines_;
    uint64_t seq_ = 0;
    uint64_t dropped_ = 0;
    // ponytail: newest 8 sinks stay open, opening a 9th closes the oldest;
    // pid reuse after a sink's process exits is the known limit.
    std::vector<rv_editor_log_sink> sinks_;
};

const char *rv_editor_log_source_name(rv_editor_log_source source);
// The local clock time `line` arrived at, "14:03:27", with ".412" when `ms`.
std::string rv_editor_log_stamp(const rv_editor_log_line &line, bool ms);

} // namespace rv_editor
