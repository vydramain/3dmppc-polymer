#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace rv_editor
{

// Where an Output line came from.
enum class rv_editor_log_source : uint8_t
{
    editor,
    build,
    runtime,
    protocol, // the dev channel's own traffic, hidden by default (TRM-02)
};

enum class rv_editor_log_level : uint8_t
{
    info,
    warning,
    error,
};

struct rv_editor_log_line
{
    uint64_t seq;
    int64_t ms;             // milliseconds since the editor started
    rv_editor_log_source source;
    rv_editor_log_level level;
    std::string text;
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

    // Splits `bytes` from a process stream into lines; a trailing partial line
    // waits in `partial` for the next chunk. Severity comes from the text.
    void add_stream(rv_editor_log_source source, std::string &partial, std::string_view bytes);
    // Whatever `partial` still holds, as a last line (the stream has ended).
    void flush_stream(rv_editor_log_source source, std::string &partial);

    void clear();

    const std::deque<rv_editor_log_line> &lines() const { return lines_; }
    uint64_t dropped() const { return dropped_; }
    // Changes every time a line is added or the log is cleared.
    uint64_t revision() const { return seq_; }

private:
    std::deque<rv_editor_log_line> lines_;
    uint64_t seq_ = 0;
    uint64_t dropped_ = 0;
};

const char *rv_editor_log_source_name(rv_editor_log_source source);

} // namespace rv_editor
