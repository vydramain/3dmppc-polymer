// Process log files, written off the frame thread.

#include "log/rv_editor_log_file.hpp"

#include "pdk/rv_err.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <system_error>
#include <utility>

namespace rv_editor
{

namespace
{

// Gap mark in a log file. The channel lost these lines; they need not be this process's.
constexpr const char *dropped_marker_head = "[";
constexpr const char *dropped_marker_tail = " log lines dropped before they reached this file]\n";

// Same key for the same file: symlinks and "./" resolved, the file itself may not exist yet.
std::string file_key(const std::filesystem::path &path)
{
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return canonical.string();
    }
    std::error_code abs_ec;
    const std::filesystem::path full = std::filesystem::absolute(path, abs_ec);
    return (abs_ec ? path : full).lexically_normal().string();
}

} // namespace

rv_editor_log_file::rv_editor_log_file()
    : writer_(&rv_editor_log_file::run, this)
{
}

rv_editor_log_file::~rv_editor_log_file()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    writer_.join();
}

int rv_editor_log_file::open(int64_t pid, const std::filesystem::path &path, const rv_editor_log &log, std::string &error)
{
    if (path.empty()) {
        error = "log file path is empty";
        return RV_ERR_INVAL;
    }
    const std::string name = path.string();
    const std::string key = file_key(path);
    const auto clash = std::find_if(files_.begin(), files_.end(), [&key, pid](const file_entry &entry) {
        return entry.key == key && entry.pid != pid;
    });
    if (clash != files_.end()) {
        error = name + " is already written for pid " + std::to_string(clash->pid);
        return RV_ERR_INVAL;
    }

    // The old file for this pid goes; otherwise the oldest goes when the set is full.
    const size_t same = index_of(pid);
    if (same < files_.size()) {
        close_at(same);
    } else if (files_.size() >= max_files) {
        close_at(0);
    }

    const uint64_t id = next_id_++;
    push(job{ .what = job::kind::attach, .id = id, .path = name, .line = {} });
    files_.push_back({ pid, id, name, key, log.revision() });
    for (const rv_editor_log_line &line : log.lines()) {
        if (line.pid == pid) {
            queue_line(id, line);
        }
    }
    return RV_OK;
}

void rv_editor_log_file::feed(const rv_editor_log &log)
{
    // clear() resets dropped() to zero, so a value below the last one counts what was evicted since.
    const uint64_t dropped = log.dropped();
    const uint64_t evicted = dropped >= dropped_seen_ ? dropped - dropped_seen_ : dropped;
    dropped_seen_ = dropped;
    if (files_.empty()) {
        return;
    }
    const std::deque<rv_editor_log_line> &lines = log.lines();

    uint64_t from = files_.front().last_seq;
    for (const file_entry &entry : files_) {
        from = std::min(from, entry.last_seq);
    }
    // Only evicted lines this file never read are lost to it. The seq gap also counts
    // clear() bumps, so the eviction count bounds it.
    for (const file_entry &entry : files_) {
        const bool behind = !lines.empty() && lines.front().seq > entry.last_seq + 1;
        const uint64_t unread = behind ? lines.front().seq - entry.last_seq - 1 : 0;
        const uint64_t lost = std::min(evicted, unread);
        if (lost > 0) {
            queue_text(entry.id, std::string(dropped_marker_head) + std::to_string(lost) + dropped_marker_tail);
        }
    }
    const auto first = std::upper_bound(lines.begin(), lines.end(), from, [](uint64_t seq, const rv_editor_log_line &line) {
        return seq < line.seq;
    });
    for (auto it = first; it != lines.end(); ++it) {
        const size_t index = index_of(it->pid);
        if (index < files_.size() && it->seq > files_[index].last_seq) {
            queue_line(files_[index].id, *it);
        }
    }
    // Every open file has now read up to the newest line.
    for (file_entry &entry : files_) {
        entry.last_seq = log.revision();
    }
}

int rv_editor_log_file::poll(std::string &error)
{
    uint64_t id = 0;
    if (take_failure(id, error)) {
        // The writer already closed the file; forget it so path_for stops naming it.
        const auto it = std::find_if(files_.begin(), files_.end(), [id](const file_entry &entry) {
            return entry.id == id;
        });
        if (it != files_.end()) {
            files_.erase(it);
        }
        return RV_ERR_IO;
    }
    return RV_OK;
}

std::string rv_editor_log_file::path_for(int64_t pid) const
{
    const size_t index = index_of(pid);
    return index < files_.size() ? files_[index].path : std::string{};
}

size_t rv_editor_log_file::index_of(int64_t pid) const
{
    for (size_t i = 0; i < files_.size(); ++i) {
        if (files_[i].pid == pid) {
            return i;
        }
    }
    return files_.size();
}

void rv_editor_log_file::close_at(size_t index)
{
    push(job{ .what = job::kind::detach, .id = files_[index].id, .path = {}, .line = {} });
    files_.erase(files_.begin() + static_cast<std::ptrdiff_t>(index));
}

void rv_editor_log_file::queue_line(uint64_t id, const rv_editor_log_line &line)
{
    queue_text(id,
        rv_editor_log_stamp(line, true) + ' ' + rv_editor_log_source_name(line.source) + ' ' +
            rv_editor_log_channel_name(line.channel) + ' ' + line.text + '\n');
}

void rv_editor_log_file::queue_text(uint64_t id, std::string text)
{
    push(job{ .what = job::kind::text, .id = id, .path = {}, .line = std::move(text) });
}

void rv_editor_log_file::push(job &&item)
{
    {
        std::lock_guard lock(mutex_);
        jobs_.push_back(std::move(item));
    }
    wake_.notify_one();
}

bool rv_editor_log_file::take_failure(uint64_t &id, std::string &message)
{
    std::lock_guard lock(mutex_);
    if (failures_.empty()) {
        return false;
    }
    id = failures_.front().id;
    message = std::move(failures_.front().message);
    failures_.pop_front();
    return true;
}

void rv_editor_log_file::fail(uint64_t id, std::string message)
{
    std::lock_guard lock(mutex_);
    failures_.push_back({ id, std::move(message) });
}

void rv_editor_log_file::run()
{
    std::map<uint64_t, open_file> files;
    for (;;) {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] {
            return stopping_ || !jobs_.empty();
        });
        if (jobs_.empty()) {
            break;
        }
        job item = std::move(jobs_.front());
        jobs_.pop_front();
        lock.unlock();
        apply(files, item);
    }
    for (const auto &entry : files) {
        std::fclose(entry.second.handle);
    }
}

void rv_editor_log_file::apply(std::map<uint64_t, open_file> &files, job &item)
{
    if (item.what == job::kind::attach) {
        attach(files, item);
        return;
    }
    const auto it = files.find(item.id);
    // Lines for a file that already failed or closed are dropped; poll reports the failure.
    if (it == files.end()) {
        return;
    }
    if (item.what == job::kind::detach) {
        std::fclose(it->second.handle);
        files.erase(it);
        return;
    }

    errno = 0;
    FILE *handle = it->second.handle;
    const bool ok = std::fwrite(item.line.data(), 1, item.line.size(), handle) == item.line.size() && std::fflush(handle) == 0;
    if (ok) {
        return;
    }
    const int err = errno;
    fail(item.id, it->second.path + ": " + std::strerror(err));
    std::fclose(handle);
    files.erase(it);
}

void rv_editor_log_file::attach(std::map<uint64_t, open_file> &files, const job &item)
{
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(item.path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    if (ec) {
        fail(item.id, item.path + ": " + ec.message());
        return;
    }

    errno = 0;
    FILE *handle = std::fopen(item.path.c_str(), "w");
    if (handle == nullptr) {
        const int err = errno;
        fail(item.id, item.path + ": " + std::strerror(err));
        return;
    }
    files[item.id] = { handle, item.path };
}

} // namespace rv_editor
