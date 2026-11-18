#include "build/rv_editor_build_alert.hpp"

#include <deque>

namespace rv_editor
{

std::string rv_editor_build_alert_reason(const rv_editor_log &log,
    uint64_t first_seq,
    const std::string &exit_text,
    const rv_editor_progress &progress)
{
    for (const rv_editor_run &run : progress.runs) {
        if (run.end == rv_editor_run_end::failed && !run.reason.empty()) {
            return run.reason;
        }
    }
    const std::deque<rv_editor_log_line> &lines = log.lines();
    for (auto it = lines.rbegin(); it != lines.rend() && it->seq >= first_seq; ++it) {
        const bool burner = it->source == rv_editor_log_source::build || it->source == rv_editor_log_source::candidate;
        if (burner && it->level == rv_editor_log_level::error) {
            return it->text;
        }
    }
    return exit_text;
}

} // namespace rv_editor
