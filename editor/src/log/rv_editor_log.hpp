#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace rv_editor
{

// Where an Output line came from.
enum class rv_editor_log_source : uint8_t {
    editor,
    build,
    candidate, // release-candidate builds, separate from ordinary dev builds
    runtime,
    protocol, // the dev channel's own traffic, hidden by default
    count,
};

enum class rv_editor_log_level : uint8_t {
    info,
    warning,
    error,
};

// Which process stream a line is a raw copy of; none for the editor's own lines.
enum class rv_editor_log_channel : uint8_t {
    none,
    out,
    err,
};

struct rv_editor_log_line {
    uint64_t seq;
    int64_t ms; // milliseconds since the editor started
    rv_editor_log_source source;
    rv_editor_log_level level;
    rv_editor_log_channel channel;
    int64_t pid;  // 0 = the editor itself
    uint32_t run; // 0 = outside any run
    std::string text;
};

// Output's model: every process line the editor keeps, oldest first. Bounded
// past `capacity` the oldest lines go and `dropped` counts them. The channel
// only keeps lines; rv_editor_log_file writes them to files. It lives outside
// the UI, so closing an Output pane loses nothing.
class rv_editor_log
{
public:
    static constexpr size_t capacity = 20000;
    // One line longer than this is cut, and says so.
    static constexpr size_t line_max = 4096;

    // channel/pid/run default to none/0/0: the editor's own line, outside any run.
    void add(rv_editor_log_source source,
        rv_editor_log_level level,
        std::string_view text,
        rv_editor_log_channel channel = rv_editor_log_channel::none,
        int64_t pid = 0,
        uint32_t run = 0);

    // Splits `bytes` from a process stream into lines; a trailing partial line
    // waits in `partial` for the next chunk. Severity comes from the text.
    void add_stream(rv_editor_log_source source,
        std::string &partial,
        std::string_view bytes,
        rv_editor_log_channel channel = rv_editor_log_channel::none,
        int64_t pid = 0,
        uint32_t run = 0);
    // Whatever `partial` still holds, as a last line (the stream has ended).
    void flush_stream(rv_editor_log_source source,
        std::string &partial,
        rv_editor_log_channel channel = rv_editor_log_channel::none,
        int64_t pid = 0,
        uint32_t run = 0);

    void clear();

    const std::deque<rv_editor_log_line> &lines() const
    {
        return lines_;
    }
    uint64_t dropped() const
    {
        return dropped_;
    }
    // Changes every time a line is added or the log is cleared.
    uint64_t revision() const
    {
        return seq_;
    }

    rv_editor_log() = default;
    rv_editor_log(const rv_editor_log &) = delete;
    rv_editor_log &operator=(const rv_editor_log &) = delete;

private:
    std::deque<rv_editor_log_line> lines_;
    uint64_t seq_ = 0;
    uint64_t dropped_ = 0;
};

const char *rv_editor_log_source_name(rv_editor_log_source source);
// Channel name in exported log text: "out", "err", or "-" for the editor's own lines.
const char *rv_editor_log_channel_name(rv_editor_log_channel channel);
// Three-letter code of a level in exported log text: "ERR", "WRN", or "INF".
const char *rv_editor_log_level_code(rv_editor_log_level level);
// The local clock time `line` arrived at, "14:03:27", with ".412" when `ms`.
std::string rv_editor_log_stamp(const rv_editor_log_line &line, bool ms);

} // namespace rv_editor
