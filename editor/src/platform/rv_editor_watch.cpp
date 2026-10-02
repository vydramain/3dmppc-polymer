// Project file watching on Linux: one inotify descriptor, one watch per directory.

#include "platform/rv_editor_watch.hpp"

#include <cerrno>
#include <cstring>
#include <sys/inotify.h>
#include <system_error>
#include <unistd.h>

namespace rv_editor
{

namespace
{

constexpr uint32_t rv_editor_watch_mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM |
    IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB | IN_DONT_FOLLOW | IN_ONLYDIR;

} // namespace

rv_editor_watch::~rv_editor_watch()
{
    stop();
}

bool rv_editor_watch::skipped(const std::string &name)
{
    return name == ".git" || name == ".mppcburn" || name == ".3dmppc-editor";
}

bool rv_editor_watch::start(const std::filesystem::path &root, std::string &error)
{
    stop();
    root_ = root;
    fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd_ < 0) {
        error = std::string("inotify: ") + std::strerror(errno);
        return false;
    }
    bool overflowed = false;
    add_tree(root, overflowed);
    if (dirs_.empty()) {
        error = root.string() + ": cannot watch";
        stop();
        return false;
    }
    return true;
}

void rv_editor_watch::stop()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    dirs_.clear();
}

void rv_editor_watch::add_tree(const std::filesystem::path &dir, bool &overflowed)
{
    if (dirs_.size() >= watch_max) {
        overflowed = true;
        return;
    }
    const int wd = ::inotify_add_watch(fd_, dir.c_str(), rv_editor_watch_mask);
    if (wd < 0) {
        overflowed = overflowed || errno == ENOSPC;
        return;
    }
    dirs_[wd] = dir;

    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        // symlink_status: a link to a directory is shown, never walked into.
        std::error_code sec;
        if (!std::filesystem::is_directory(it->symlink_status(sec)) || skipped(it->path().filename().string())) {
            continue;
        }
        add_tree(it->path(), overflowed);
    }
}

void rv_editor_watch::poll(std::vector<rv_editor_watch_event> &out, bool &overflowed)
{
    if (fd_ < 0) {
        return;
    }
    alignas(inotify_event) char buf[16384];
    bool rewatch = false;
    for (;;) {
        const ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n <= 0) {
            break; // EAGAIN: nothing more now
        }
        for (ssize_t off = 0; off < n;) {
            const inotify_event *ev = reinterpret_cast<const inotify_event *>(buf + off);
            off += static_cast<ssize_t>(sizeof(inotify_event) + ev->len);
            if (ev->mask & IN_Q_OVERFLOW) {
                overflowed = true;
                continue;
            }
            const auto it = dirs_.find(ev->wd);
            if (it == dirs_.end()) {
                continue;
            }
            if (ev->mask & IN_IGNORED) {
                dirs_.erase(it);
                continue;
            }
            const std::filesystem::path dir = it->second;
            if (ev->len == 0) {
                // The watched directory itself went away or moved.
                if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                    out.push_back({ dir, true, true });
                }
                continue;
            }
            const std::string name(ev->name);
            if (skipped(name)) {
                continue;
            }
            const std::filesystem::path path = dir / name;
            const bool is_dir = (ev->mask & IN_ISDIR) != 0;
            const bool gone = (ev->mask & (IN_DELETE | IN_MOVED_FROM)) != 0;
            // A directory created or moved in is watched from now on; one that
            // moved away leaves watches under old paths, so all are redone.
            if (is_dir && !gone && (ev->mask & (IN_CREATE | IN_MOVED_TO))) {
                add_tree(path, overflowed);
            }
            if (is_dir && gone) {
                rewatch = true;
            }
            out.push_back({ path, is_dir, gone });
        }
    }
    if (rewatch) {
        const std::filesystem::path root = root_;
        std::string error;
        start(root, error);
        overflowed = true;
    }
}

} // namespace rv_editor
