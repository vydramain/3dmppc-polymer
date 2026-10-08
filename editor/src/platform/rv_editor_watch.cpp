// Project file watching on Linux: one inotify descriptor, one watch per directory.

#include "platform/rv_editor_watch.hpp"

#include <cerrno>
#include <cstring>
#include <sys/inotify.h>
#include <system_error>
#include <unistd.h>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

constexpr uint32_t rv_editor_watch_mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO |
    IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB | IN_DONT_FOLLOW | IN_ONLYDIR;

// Room for several inotify_event records, each name up to NAME_MAX (255) bytes plus NUL.
constexpr size_t inotify_buffer_bytes = 16384;

// Reason for a failed call on `path`; ENOSPC is the kernel's watch limit, a missing path is a race.
int fail_with(const std::filesystem::path &path, int err, std::string &error)
{
    error = path.string() + ": " + std::error_code(err, std::generic_category()).message();
    // ENOSPC: the kernel's watch limit; ENOMEM: kernel memory for the watch.
    if (err == ENOSPC || err == ENOMEM) {
        return RV_ERR_NOMEM;
    }
    if (err == ENOENT || err == ENOTDIR) {
        return RV_ERR_NOENT;
    }
    return RV_ERR_IO;
}

} // namespace

rv_editor_watch::~rv_editor_watch()
{
    stop();
}

bool rv_editor_watch::skipped(const std::string &name)
{
    return name == ".git" || name == ".mppcburn" || name == ".3dmppc-editor";
}

int rv_editor_watch::start(const std::filesystem::path &root, std::string &error)
{
    if (fd_ >= 0) {
        error = "watcher is already started";
        return RV_ERR_INVAL;
    }
    fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd_ < 0) {
        error = std::string("inotify: ") + std::strerror(errno);
        return RV_ERR_IO;
    }
    return add_tree(root, error);
}

void rv_editor_watch::stop()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    dirs_.clear();
}

int rv_editor_watch::watch_tree(const std::filesystem::path &dir, std::string &error)
{
    if (fd_ < 0) {
        error = "watcher is not started";
        return RV_ERR_INVAL;
    }
    return add_tree(dir, error);
}

int rv_editor_watch::watch_dir(const std::filesystem::path &dir, std::string &error)
{
    if (dirs_.size() >= watch_max) {
        error = dir.string() + ": watch limit reached";
        return RV_ERR_NOMEM;
    }
    const int wd = ::inotify_add_watch(fd_, dir.c_str(), rv_editor_watch_mask);
    if (wd < 0) {
        return fail_with(dir, errno, error);
    }
    dirs_[wd] = dir;
    return RV_OK;
}

int rv_editor_watch::subdirs(const std::filesystem::path &dir, std::vector<std::filesystem::path> &out, std::string &error)
{
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    const std::filesystem::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        // symlink_status: a link to a directory is shown, never walked into.
        std::error_code sec;
        const std::filesystem::file_status status = it->symlink_status(sec);
        if (sec) {
            // Vanished between listing and this check: not a failure.
            if (sec.value() == ENOENT || sec.value() == ENOTDIR) {
                continue;
            }
            return fail_with(it->path(), sec.value(), error);
        }
        if (std::filesystem::is_directory(status) && !skipped(it->path().filename().string())) {
            out.push_back(it->path());
        }
    }
    return ec ? fail_with(dir, ec.value(), error) : RV_OK;
}

int rv_editor_watch::add_tree(const std::filesystem::path &dir, std::string &error)
{
    const int watched = watch_dir(dir, error);
    if (watched != RV_OK) {
        return watched;
    }
    std::vector<std::filesystem::path> subs;
    const int listed = subdirs(dir, subs, error);
    if (listed != RV_OK) {
        return listed;
    }
    for (const std::filesystem::path &sub : subs) {
        // A directory that vanished meanwhile is not part of the tree any more; the walk goes on.
        const int sub_rc = add_tree(sub, error);
        if (sub_rc != RV_OK && sub_rc != RV_ERR_NOENT) {
            return sub_rc;
        }
    }
    return RV_OK;
}

int rv_editor_watch::read_events(std::vector<char> &raw, std::string &error)
{
    if (fd_ < 0) {
        error = "watcher is not started";
        return RV_ERR_INVAL;
    }
    char buf[inotify_buffer_bytes];
    for (;;) {
        const ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && errno != EAGAIN) {
            error = std::string("inotify read: ") + std::strerror(errno);
            return RV_ERR_IO;
        }
        if (n <= 0) {
            break; // EAGAIN: nothing more now
        }
        raw.insert(raw.end(), buf, buf + n);
    }
    return RV_OK;
}

void rv_editor_watch::translate(const std::vector<char> &raw, std::vector<rv_editor_watch_event> &out)
{
    for (size_t off = 0; off + sizeof(inotify_event) <= raw.size();) {
        // Copied out: the record in raw is not aligned for inotify_event.
        inotify_event ev;
        std::memcpy(&ev, raw.data() + off, sizeof(ev));
        if (ev.len > raw.size() - off - sizeof(ev)) {
            break; // truncated record: nothing after it can be trusted
        }
        const char *name = raw.data() + off + sizeof(ev);
        off += sizeof(ev) + ev.len;
        if (ev.mask & IN_Q_OVERFLOW) {
            out.push_back({ .path = {}, .lost = true });
            continue;
        }
        const auto it = dirs_.find(ev.wd);
        if (it == dirs_.end()) {
            continue;
        }
        if (ev.mask & IN_IGNORED) {
            dirs_.erase(it);
            continue;
        }
        const std::filesystem::path dir = it->second;
        if (ev.len == 0) {
            // The watched directory itself went away or moved.
            if (ev.mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                out.push_back({ .path = dir, .directory = true, .removed = true });
            }
            continue;
        }
        const std::string entry(name);
        if (skipped(entry)) {
            continue;
        }
        const bool is_dir = (ev.mask & IN_ISDIR) != 0;
        const bool gone = (ev.mask & (IN_DELETE | IN_MOVED_FROM)) != 0;
        const bool appeared = is_dir && (ev.mask & (IN_CREATE | IN_MOVED_TO)) != 0;
        out.push_back({ .path = dir / entry, .directory = is_dir, .removed = gone, .appeared = appeared });
    }
}

} // namespace rv_editor
