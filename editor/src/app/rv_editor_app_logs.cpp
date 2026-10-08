// Each runtime session and each development build keeps its whole log in a file.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rv_editor
{

namespace
{

// Session and build log file extension and directory names
constexpr std::string_view log_file_extension = ".log";

// Session log filename format: YYYYmmdd-HHMMSS-number-pid.log and directory
constexpr std::string_view sessions_dir_name = "sessions";
constexpr std::string_view session_log_part_separator = "-";
constexpr size_t session_log_name_min_length = log_file_extension.size() + 1;
constexpr size_t session_log_name_parts = 4;
constexpr size_t session_log_part_date_index = 0;
constexpr size_t session_log_part_time_index = 1;
constexpr size_t session_log_part_number_index = 2;
constexpr size_t session_log_part_pid_index = 3;
constexpr size_t session_log_timestamp_digits = 8; // YYYYmmdd
constexpr size_t session_log_time_digits = 6;      // HHMMSS
constexpr size_t session_timestamp_buffer_size = 32;

// Build log directory name; must match builds_dir_name in rv_editor_panes.cpp and
// rv_editor_build.cpp
constexpr std::string_view builds_dir_name = "builds";

// Session logs retention limit
constexpr size_t session_logs_kept = 20;

// "YYYYmmdd-HHMMSS" for the wall-clock instant a session started.
std::string rv_editor_app_timestamp(std::chrono::system_clock::time_point t)
{
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[session_timestamp_buffer_size];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

// True for a session log's own name: <8 digits>-<6 digits>-<digits>-<digits>.log.
bool rv_editor_app_is_session_log(const std::string &name)
{
    const auto digits = [](std::string_view s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
            return c >= '0' && c <= '9';
        });
    };
    if (name.size() < session_log_name_min_length ||
        name.compare(name.size() - log_file_extension.size(), log_file_extension.size(), log_file_extension) != 0) {
        return false;
    }
    const std::string stem = name.substr(0, name.size() - log_file_extension.size());
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= stem.size(); ++i) {
        if (i != stem.size() && stem[i] != session_log_part_separator[0]) {
            continue;
        }
        parts.push_back(stem.substr(start, i - start));
        start = i + 1;
    }
    return parts.size() == session_log_name_parts &&
        parts[session_log_part_date_index].size() == session_log_timestamp_digits &&
        digits(parts[session_log_part_date_index]) && parts[session_log_part_time_index].size() == session_log_time_digits &&
        digits(parts[session_log_part_time_index]) && digits(parts[session_log_part_number_index]) &&
        digits(parts[session_log_part_pid_index]);
}

// Keeps only the session_logs_kept newest session logs in `dir`; only files matching the
// session-log name pattern are ever removed.
void rv_editor_app_prune_session_logs(const std::filesystem::path &dir)
{
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> logs;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !rv_editor_app_is_session_log(it->path().filename().string())) {
            continue;
        }
        logs.emplace_back(std::filesystem::last_write_time(it->path(), ec), it->path());
    }
    std::sort(logs.begin(), logs.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    for (size_t i = session_logs_kept; i < logs.size(); ++i) {
        std::filesystem::remove(logs[i].second, ec);
    }
}

} // namespace

void rv_editor_app_attach_session_log(rv_editor_app &app)
{
    if (app.project.state_dir.empty()) {
        return;
    }
    const std::filesystem::path sessions = app.project.state_dir / sessions_dir_name;
    const std::string name = rv_editor_app_timestamp(app.session.started_at()) + std::string(session_log_part_separator) +
        std::to_string(app.session.number()) + std::string(session_log_part_separator) + std::to_string(app.session.pid()) +
        std::string(log_file_extension);
    app.log.attach_file(app.session.pid(), (sessions / name).string());
    rv_editor_app_prune_session_logs(sessions);
}

void rv_editor_app_attach_build_log(rv_editor_app &app)
{
    if (app.project.cache_dir.empty()) {
        return;
    }
    const std::string filename = std::to_string(app.build.number()) + std::string(log_file_extension);
    const std::filesystem::path path = app.project.cache_dir / builds_dir_name / filename;
    app.log.attach_file(app.build.pid(), path.string());
}

} // namespace rv_editor
