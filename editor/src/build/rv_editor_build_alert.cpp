#include "build/rv_editor_build_alert.hpp"

#include <deque>

namespace rv_editor
{

std::string rv_editor_build_alert_reason(const rv_editor_log &log, uint64_t first_seq, const std::string &exit_text)
{
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
