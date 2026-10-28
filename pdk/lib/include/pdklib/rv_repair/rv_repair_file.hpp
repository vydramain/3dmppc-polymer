#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "pdk/rv_err.h"

// File operations of the repair program: backup copies and atomic rewrite of a disc
// source file. Which files to touch and which time stamp to use is the program's call.

namespace rv_pdklib
{

namespace detail
{

inline constexpr std::string_view rv_repair_env_state = "XDG_STATE_HOME";
inline constexpr std::string_view rv_repair_env_home = "HOME";
inline constexpr std::string_view rv_repair_state_fallback = ".local/state";
inline constexpr std::string_view rv_repair_dir = "3dmppc/repair";
inline constexpr std::string_view rv_repair_time_format = "%Y%m%d-%H%M%S";
// "YYYYmmdd-HHMMSS" is 15 characters; the rest is room for the terminator.
inline constexpr size_t rv_repair_time_buf = 32;
inline constexpr std::string_view rv_repair_tmp_mark = ".rv-tmp-";
// Bounds the search for a temporary name that no file holds yet.
inline constexpr int rv_repair_tmp_attempts = 100;
// Sequence of temporary names; the search for a free name, not this value, makes them unique.
inline std::atomic<unsigned> rv_repair_tmp_seq{ 0 };

// Value of an environment variable when it is absolute; empty otherwise.
inline std::string rv_repair_env_abs(std::string_view name)
{
    const char *value = std::getenv(std::string(name).c_str());
    if (value == nullptr || !std::filesystem::path(value).is_absolute()) {
        return {};
    }
    return value;
}

// Removes `file` when it is not empty; a failed removal is added to `error`.
inline void rv_repair_drop(const std::filesystem::path &file, std::string &error)
{
    if (file.empty()) {
        return;
    }
    std::error_code rm;
    std::filesystem::remove(file, rm);
    if (rm) {
        error += "; " + file.string() + " stays: " + rm.message();
    }
}

// Records an I/O failure at `at`, removes `drop`, returns RV_ERR_IO.
inline int rv_repair_io_fail(const std::filesystem::path &drop,
    const std::filesystem::path &at,
    std::string_view action,
    const std::error_code &ec,
    std::string &error)
{
    error = std::string(action) + " " + at.string() + ": " + ec.message();
    rv_repair_drop(drop, error);
    return RV_ERR_IO;
}

// Writes `text` to `file`. On failure `error` gives the step; the file may be partly written.
inline int rv_repair_write_bytes(const std::filesystem::path &file, std::string_view text, std::string &error)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot open " + file.string();
        return RV_ERR_IO;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        error = "write failed: " + file.string();
        return RV_ERR_IO;
    }
    out.close();
    if (!out) {
        error = "close failed: " + file.string();
        return RV_ERR_IO;
    }
    return RV_OK;
}

} // namespace detail

// "YYYYmmdd-HHMMSS" in local time into `stamp`; `stamp` is set only on RV_OK.
inline int rv_repair_timestamp(std::chrono::system_clock::time_point t, std::string &stamp, std::string &error)
{
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
    if (localtime_r(&tt, &tm) == nullptr) {
        error = "timestamp: time is not representable in local time";
        return RV_ERR_INVAL;
    }
    char buf[detail::rv_repair_time_buf];
    const size_t n = std::strftime(buf, sizeof(buf), detail::rv_repair_time_format.data(), &tm);
    if (n == 0) {
        error = "timestamp: format does not fit the buffer";
        return RV_ERR_INVAL;
    }
    stamp.assign(buf, n);
    return RV_OK;
}

// $XDG_STATE_HOME or $HOME/.local/state, then 3dmppc/repair. A relative value is ignored, as
// the XDG spec says. Neither usable: RV_ERR_NOENT.
inline int rv_repair_backup_root(std::filesystem::path &root, std::string &error)
{
    const std::string xdg = detail::rv_repair_env_abs(detail::rv_repair_env_state);
    if (!xdg.empty()) {
        root = std::filesystem::path(xdg) / detail::rv_repair_dir;
        return RV_OK;
    }
    const std::string home = detail::rv_repair_env_abs(detail::rv_repair_env_home);
    if (home.empty()) {
        error = "no backup root: $XDG_STATE_HOME and $HOME are unset or not absolute";
        return RV_ERR_NOENT;
    }
    root = std::filesystem::path(home) / detail::rv_repair_state_fallback / detail::rv_repair_dir;
    return RV_OK;
}

// Copies `rel`, a file of `project`, into the backup root; `copy` gets the copy's path.
// The copy is root / <absolute project path> / <rel dir> / "<file name>.<stamp>".
// An existing copy is never overwritten: RV_ERR_BUSY. The same stamp names the same copy,
// so a retry needs a new stamp.
inline int rv_repair_backup(const std::filesystem::path &project,
    const std::filesystem::path &rel,
    std::string_view stamp,
    std::filesystem::path &copy,
    std::string &error)
{
    if (stamp.empty()) {
        error = "backup: empty time stamp";
        return RV_ERR_INVAL;
    }
    const bool escapes = std::ranges::any_of(rel, [](const std::filesystem::path &part) {
        return part == "..";
    });
    if (rel.empty() || rel.is_absolute() || escapes) {
        error = "backup: " + rel.string() + " is not a path inside the project";
        return RV_ERR_INVAL;
    }
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(project, ec).lexically_normal();
    if (ec) {
        return detail::rv_repair_io_fail({}, project, "absolute path of", ec, error);
    }
    std::filesystem::path root;
    if (const int rc = rv_repair_backup_root(root, error); rc != RV_OK) {
        return rc;
    }
    const std::filesystem::path source = project / rel;
    ec.clear();
    const std::filesystem::file_status src = std::filesystem::status(source, ec);
    // A regular file used as a directory (a.txt/x) is also "not there".
    if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory) {
        return detail::rv_repair_io_fail({}, source, "stat", ec, error);
    }
    if (src.type() == std::filesystem::file_type::not_found) {
        error = "backup: no such file " + source.string();
        return RV_ERR_NOENT;
    }
    if (!std::filesystem::is_regular_file(src)) {
        error = "backup: not a regular file " + source.string();
        return RV_ERR_INVAL;
    }
    const std::filesystem::path dir = root / abs.relative_path() / rel.parent_path();
    copy = dir / (rel.filename().string() + "." + std::string(stamp));
    ec.clear();
    const std::filesystem::file_status st = std::filesystem::symlink_status(copy, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        return detail::rv_repair_io_fail({}, copy, "stat", ec, error);
    }
    if (st.type() != std::filesystem::file_type::not_found) {
        error = "backup already taken: " + copy.string();
        return RV_ERR_BUSY;
    }
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return detail::rv_repair_io_fail({}, dir, "create", ec, error);
    }
    std::filesystem::copy_file(source, copy, std::filesystem::copy_options::none, ec);
    // Another writer took the name after the check above; its file is not ours to remove.
    if (ec == std::errc::file_exists) {
        error = "backup already taken: " + copy.string();
        return RV_ERR_BUSY;
    }
    if (ec) {
        return detail::rv_repair_io_fail(copy, copy, "copy to", ec, error);
    }
    return RV_OK;
}

// Replaces the bytes of the existing regular file `target` with `text`. The text goes to a
// temporary file beside the target, which is renamed over it; any failure leaves the target as it was.
// A symlink is refused: the rename would replace the link, not the file it points to.
inline int rv_repair_write_atomic(const std::filesystem::path &target, std::string_view text, std::string &error)
{
    std::error_code ec;
    const std::filesystem::file_status link = std::filesystem::symlink_status(target, ec);
    // A regular file used as a directory (f/x) is also "not there".
    if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory) {
        return detail::rv_repair_io_fail({}, target, "stat", ec, error);
    }
    if (link.type() == std::filesystem::file_type::not_found) {
        error = "write: no such file " + target.string();
        return RV_ERR_NOENT;
    }
    if (std::filesystem::is_symlink(link)) {
        error = "write: target is a symlink, not rewritten: " + target.string();
        return RV_ERR_INVAL;
    }
    if (!std::filesystem::is_regular_file(link)) {
        error = "write: not a regular file " + target.string();
        return RV_ERR_INVAL;
    }
    std::filesystem::path tmp;
    for (int i = 0;; ++i) {
        if (i == detail::rv_repair_tmp_attempts) {
            error = "write: no free temporary name beside " + target.string();
            return RV_ERR_BUSY;
        }
        tmp = target.string() + std::string(detail::rv_repair_tmp_mark) + std::to_string(detail::rv_repair_tmp_seq++);
        ec.clear();
        const std::filesystem::file_status st = std::filesystem::symlink_status(tmp, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) {
            return detail::rv_repair_io_fail({}, tmp, "stat", ec, error);
        }
        if (st.type() == std::filesystem::file_type::not_found) {
            break;
        }
    }
    if (const int rc = detail::rv_repair_write_bytes(tmp, text, error); rc != RV_OK) {
        detail::rv_repair_drop(tmp, error);
        return rc;
    }
    // The rename would otherwise give the target the temporary file's default mode.
    ec.clear();
    const std::filesystem::perms mode = std::filesystem::status(target, ec).permissions();
    if (ec) {
        return detail::rv_repair_io_fail(tmp, target, "stat", ec, error);
    }
    std::filesystem::permissions(tmp, mode, ec);
    if (ec) {
        return detail::rv_repair_io_fail(tmp, tmp, "chmod", ec, error);
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        return detail::rv_repair_io_fail(tmp, target, "rename", ec, error);
    }
    return RV_OK;
}

} // namespace rv_pdklib
