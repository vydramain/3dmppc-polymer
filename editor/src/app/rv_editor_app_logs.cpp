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

// "YYYYmmdd-HHMMSS" for the wall-clock instant a session started.
std::string rv_editor_app_timestamp(std::chrono::system_clock::time_point t)
{
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

// True for a session log's own name: <8 digits>-<6 digits>-<digits>-<digits>.log.
bool rv_editor_app_is_session_log(const std::string &name)
{
    const auto digits = [](std::string_view s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    if (name.size() < 5 || name.compare(name.size() - 4, 4, ".log") != 0) {
        return false;
    }
    const std::string stem = name.substr(0, name.size() - 4);
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= stem.size(); ++i) {
        if (i != stem.size() && stem[i] != '-') {
            continue;
        }
        parts.push_back(stem.substr(start, i - start));
        start = i + 1;
    }
    return parts.size() == 4 && parts[0].size() == 8 && digits(parts[0]) && parts[1].size() == 6 &&
        digits(parts[1]) && digits(parts[2]) && digits(parts[3]);
}

// Keeps only the 20 newest session logs in `dir`; only files matching the
// session-log name pattern are ever removed.
void rv_editor_app_prune_session_logs(const std::filesystem::path &dir)
{
    constexpr size_t keep = 20;
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> logs;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !rv_editor_app_is_session_log(it->path().filename().string())) {
            continue;
        }
        logs.emplace_back(std::filesystem::last_write_time(it->path(), ec), it->path());
    }
    std::sort(logs.begin(), logs.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (size_t i = keep; i < logs.size(); ++i) {
        std::filesystem::remove(logs[i].second, ec);
    }
}

} // namespace

void rv_editor_app_attach_session_log(rv_editor_app &app)
{
    if (app.project.state_dir.empty()) {
        return;
    }
    const std::filesystem::path sessions = app.project.state_dir / "sessions";
    const std::string name = rv_editor_app_timestamp(app.session.started_at()) + "-" +
        std::to_string(app.session.number()) + "-" + std::to_string(app.session.pid()) + ".log";
    app.log.attach_file(app.session.pid(), (sessions / name).string());
    rv_editor_app_prune_session_logs(sessions);
}

void rv_editor_app_attach_build_log(rv_editor_app &app)
{
    if (app.project.cache_dir.empty()) {
        return;
    }
    const std::filesystem::path path = app.project.cache_dir / "builds" / (std::to_string(app.build.number()) + ".log");
    app.log.attach_file(app.build.pid(), path.string());
}

} // namespace rv_editor
