// The processes of a pseudo-terminal's session, read from /proc.

#include "platform/rv_editor_pty_session.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Procfs root directory containing process information
constexpr std::string_view proc_root_path = "/proc";
// Procfs stat file with process state
constexpr std::string_view proc_stat_file = "stat";
// Digit characters for matching PID directory names
constexpr std::string_view pid_digit_chars = "0123456789";
// Expected number of fields parsed from /proc/[pid]/stat after closing paren
constexpr int proc_stat_field_count = 4;
// Radix for parsing PID as decimal number
constexpr int decimal_radix = 10;

} // namespace

void rv_editor_add_reason(std::string &error, const std::string &reason)
{
    error += error.empty() ? reason : "; " + reason;
}

// Every process of session `sid` (Linux: /proc/<pid>/stat) into `out`, so a job the shell
// sent to the background leaves with it. RV_ERR_IO when /proc cannot be walked: an empty
// list would leave the session running.
int rv_editor_session_members(pid_t sid, std::vector<pid_t> &out, std::string &error)
{
    std::error_code ec;
    for (std::filesystem::directory_iterator it(proc_root_path, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name.find_first_not_of(pid_digit_chars) != std::string::npos) {
            continue;
        }
        std::ifstream stat(it->path() / proc_stat_file);
        std::string line;
        if (!std::getline(stat, line)) {
            // Gone since it was listed: nothing to signal. Still there: /proc is unreadable.
            std::error_code gone_ec;
            if (std::filesystem::exists(it->path(), gone_ec)) {
                error = "cannot read " + (it->path() / proc_stat_file).string();
                return RV_ERR_IO;
            }
            continue;
        }
        // pid (comm) state ppid pgrp session ...; comm may hold spaces and ')'.
        const size_t close = line.rfind(')');
        char state = 0;
        long ppid = 0;
        long pgrp = 0;
        long session = 0;
        if (close != std::string::npos &&
            std::sscanf(line.c_str() + close + 1, " %c %ld %ld %ld", &state, &ppid, &pgrp, &session) == proc_stat_field_count &&
            session == sid) {
            out.push_back(static_cast<pid_t>(std::strtol(name.c_str(), nullptr, decimal_radix)));
        }
    }
    if (ec) {
        error = "/proc: " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Sends `sig` to every process of session `sid`. A member that ended meanwhile is no failure.
int rv_editor_signal_session(pid_t sid, int sig, std::string &error)
{
    std::vector<pid_t> members;
    if (rv_editor_session_members(sid, members, error) != RV_OK) {
        return RV_ERR_IO;
    }
    int rc = RV_OK;
    for (const pid_t pid : members) {
        if (::kill(pid, sig) != 0 && errno != ESRCH) {
            error = std::string("signal: ") + std::strerror(errno);
            rc = RV_ERR_IO;
        }
    }
    return rc;
}

} // namespace rv_editor
