// Output's bounded line store.

#include "log/rv_editor_log.hpp"

#include <chrono>

namespace rv_editor
{

namespace
{

int64_t rv_editor_log_now()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
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

void rv_editor_log::add(rv_editor_log_source source, rv_editor_log_level level, std::string_view text)
{
    std::string kept(text.substr(0, line_max));
    if (text.size() > line_max) {
        kept += " [cut: " + std::to_string(text.size()) + " bytes]";
    }
    lines_.push_back({ ++seq_, rv_editor_log_now(), source, level, std::move(kept) });
    while (lines_.size() > capacity) {
        lines_.pop_front();
        ++dropped_;
    }
}

void rv_editor_log::add_stream(rv_editor_log_source source, std::string &partial, std::string_view bytes)
{
    while (!bytes.empty()) {
        const size_t nl = bytes.find('\n');
        if (nl == std::string_view::npos) {
            // A line that never ends is still bounded: emit it in pieces.
            partial.append(bytes);
            if (partial.size() > line_max) {
                add(source, rv_editor_log_level_of(partial), partial);
                partial.clear();
            }
            return;
        }
        partial.append(bytes.substr(0, nl));
        bytes = bytes.substr(nl + 1);
        if (!partial.empty() && partial.back() == '\r') {
            partial.pop_back();
        }
        add(source, rv_editor_log_level_of(partial), partial);
        partial.clear();
    }
}

void rv_editor_log::flush_stream(rv_editor_log_source source, std::string &partial)
{
    if (!partial.empty()) {
        add(source, rv_editor_log_level_of(partial), partial);
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
        case rv_editor_log_source::runtime: return "runtime";
        case rv_editor_log_source::protocol: return "protocol";
    }
    return "?";
}

} // namespace rv_editor
