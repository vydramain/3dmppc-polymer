// The project tree: lazy listing, watcher-driven refresh, confined file operations.

#include "files/rv_editor_files.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <map>
#include <system_error>

#include "pdk/rv_err.h"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Most recent changed paths kept before the oldest are dropped.
constexpr size_t changed_paths_max = 4096;

// File head buffer for binary detection: reads up to this many bytes to scan for null bytes
constexpr size_t file_head_read_bytes = 8192;

// Parent directory path component; used to detect path escaping and as an invalid file name.
constexpr std::string_view parent_dir_marker = "..";

// Current directory name; invalid as a file or directory to create.
constexpr std::string_view current_dir_marker = ".";

// Filesystem path separator; used in inside() to check path boundaries.
constexpr const char *path_separator = "/";

// Stop on a failure path: the first error stays, a failed close is appended to it.
// RV_ERR_ALREADY is ignored: nothing was open to close.
void stop_after_failure(rv_editor_watch &watch, std::string &error)
{
    std::string stop_error;
    if (watch.stop(stop_error) == RV_ERR_IO) {
        error += "; " + stop_error;
    }
}

} // namespace

int rv_editor_files::open(const std::filesystem::path &root, std::string &error)
{
    // The old descriptor is dropped either way, so the open goes on; the failure is reported by update().
    std::string close_error;
    if (close(close_error) == RV_ERR_IO) {
        close_error_ += (close_error_.empty() ? "" : "; ") + close_error;
    }
    // Canonical, so inside() compares like with like.
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(root, ec);
    const std::filesystem::path path = ec ? root : canonical;
    std::error_code dec;
    if (!std::filesystem::is_directory(path, dec)) {
        error = path.string() + ": " + (dec ? dec.message() : "not a folder");
        return dec && dec.value() != ENOENT && dec.value() != ENOTDIR ? RV_ERR_IO : RV_ERR_NOENT;
    }
    root_.path = path;
    root_.name = root_.path.filename().string();
    root_.dir = true;
    root_.expanded = true;
    list(root_);
    return RV_OK;
}

int rv_editor_files::start_watching(std::string &error)
{
    if (!is_open()) {
        error = "no project is open";
        return RV_ERR_INVAL;
    }
    if (watching_) {
        error = "already watched";
        return RV_ERR_INVAL;
    }
    const int rc = watch_.start(root_.path, error);
    // A partial watch at the limit still reports changes; overflow re-reads the tree.
    watching_ = rc == RV_OK || rc == RV_ERR_NOMEM;
    if (!watching_) {
        stop_after_failure(watch_, error);
        return rc;
    }
    // Watch first, then read: changes made since open() are not missed.
    list(root_);
    return rc;
}

int rv_editor_files::close(std::string &error)
{
    const int rc = watch_.stop(error);
    watching_ = false;
    root_ = {};
    stale_.clear();
    changed.clear();
    return rc;
}

void rv_editor_files::list(rv_editor_file_node &dir)
{
    // What was open under it stays open after the re-read.
    std::map<std::string, rv_editor_file_node> old;
    for (rv_editor_file_node &child : dir.children) {
        old[child.name] = std::move(child);
    }
    dir.children.clear();

    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir.path, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (rv_editor_watch::skipped(name)) {
            continue;
        }
        std::error_code sec;
        const std::filesystem::file_status link = it->symlink_status(sec);
        rv_editor_file_node node;
        node.name = name;
        node.path = it->path();
        node.symlink = std::filesystem::is_symlink(link);
        node.dir = std::filesystem::is_directory(link);
        const auto was = old.find(name);
        if (was != old.end() && was->second.dir == node.dir && !node.symlink) {
            node.expanded = was->second.expanded;
            node.listed = was->second.listed;
            node.children = std::move(was->second.children);
        }
        dir.children.push_back(std::move(node));
    }
    std::sort(dir.children.begin(), dir.children.end(), [](const rv_editor_file_node &a, const rv_editor_file_node &b) {
        return a.dir != b.dir ? a.dir : a.name < b.name;
    });
    dir.listed = true;
    for (rv_editor_file_node &child : dir.children) {
        if (child.dir && !child.symlink && child.listed) {
            list(child);
        }
    }
}

int rv_editor_files::reveal(const std::filesystem::path &path)
{
    if (!is_open()) {
        return RV_ERR_INVAL;
    }
    const std::filesystem::path rel = path.lexically_relative(root_.path);
    const bool not_below = rel.empty() || *rel.begin() == parent_dir_marker;
    if (not_below) {
        return RV_ERR_NOENT;
    }
    rv_editor_file_node *node = &root_;
    for (auto it = rel.begin(); it != rel.end(); ++it) {
        if (!node->listed) {
            list(*node);
        }
        node->expanded = true;
        rv_editor_file_node *next = nullptr;
        for (rv_editor_file_node &child : node->children) {
            if (child.name == it->string()) {
                next = &child;
            }
        }
        if (next == nullptr) {
            return RV_ERR_NOENT;
        }
        if (std::next(it) == rel.end()) {
            break;
        }
        if (!next->dir || next->symlink) {
            return RV_ERR_NOENT;
        }
        node = next;
    }
    return RV_OK;
}

void rv_editor_files::refresh()
{
    if (is_open()) {
        list(root_);
    }
}

rv_editor_file_node *rv_editor_files::find(rv_editor_file_node &node, const std::filesystem::path &path)
{
    if (node.path == path) {
        return &node;
    }
    for (rv_editor_file_node &child : node.children) {
        if (child.dir && !child.symlink && path.native().starts_with(child.path.native() + path_separator)) {
            return find(child, path);
        }
        if (child.path == path) {
            return &child;
        }
    }
    return nullptr;
}

int rv_editor_files::update(std::vector<std::string> &warnings, std::string &error)
{
    if (!close_error_.empty()) {
        warnings.push_back("the previous project's file watcher did not close: " + close_error_);
        close_error_.clear();
    }
    if (!is_open() || !watching_) {
        return RV_OK;
    }
    std::vector<char> raw;
    std::string read_error;
    const int read_rc = watch_.read_events(raw, read_error);
    // Records read before a failed read are applied first, then the failure is returned.
    std::vector<rv_editor_watch::rv_editor_watch_event> events;
    watch_.translate(raw, events);
    bool lost = false;
    bool limit = false;
    bool restart = false;
    int result = read_rc;
    std::string why;
    // First new folder that could not be watched: reported, the rest keeps being watched.
    std::string folder_error;
    for (const auto &ev : events) {
        if (ev.lost) {
            lost = true;
            continue;
        }
        if (ev.directory && ev.removed) {
            restart = true;
        }
        if (ev.appeared) {
            std::string tree_error;
            const int rc = watch_.watch_tree(ev.path, tree_error);
            if (rc == RV_ERR_NOMEM) {
                limit = true;
            } else if (rc == RV_ERR_IO && folder_error.empty()) {
                folder_error = "new folder is not watched: " + tree_error;
            }
        }
        stale_.insert(ev.path.parent_path());
        if (!ev.directory) {
            changed.push_back(ev.path);
        }
    }
    if (result == RV_OK && restart) {
        // A moved or deleted directory leaves watches on paths that no longer exist.
        // Restart is stop() then start(); a stop that fails fails the restart.
        std::string stop_error;
        const int stop_rc = watch_.stop(stop_error);
        const bool stop_failed = stop_rc == RV_ERR_IO;
        // RV_ERR_ALREADY: nothing to reset, the start goes on.
        const int rc = stop_failed ? RV_ERR_IO : watch_.start(root_.path, why);
        if (stop_failed) {
            why = stop_error;
        }
        watching_ = rc == RV_OK || rc == RV_ERR_NOMEM;
        limit = limit || rc == RV_ERR_NOMEM;
        // Restarts from the project root, so NOENT means the root itself is gone.
        if (rc == RV_ERR_NOENT) {
            why = "project folder is gone";
        }
        if (!watching_) {
            result = RV_ERR_IO;
        }
    }
    if (lost) {
        warnings.push_back("the file watcher lost track of changes; the whole tree is re-read");
    }
    if (limit) {
        warnings.push_back("watch limit reached, part of the tree is not watched; the whole tree is re-read");
    }
    if (lost || limit || restart) {
        stale_.clear();
        list(root_);
    } else {
        for (const std::filesystem::path &dir : stale_) {
            relist(dir);
        }
        stale_.clear();
    }
    // Bounded: nobody who needs this list lets it grow across frames.
    if (changed.size() > changed_paths_max) {
        changed.erase(changed.begin(), changed.end() - changed_paths_max);
    }
    if (result != RV_OK) {
        watching_ = false;
        error = read_rc != RV_OK ? read_error : why;
        stop_after_failure(watch_, error);
        return result;
    }
    // The watch keeps running: only the new folder is unwatched.
    if (!folder_error.empty()) {
        error = folder_error;
        return RV_ERR_IO;
    }
    return RV_OK;
}

void rv_editor_files::relist(const std::filesystem::path &dir)
{
    rv_editor_file_node *node = find(root_, dir);
    if (node != nullptr && node->listed) {
        list(*node);
    }
}

bool rv_editor_files::valid_name(const std::string &name, std::string &error)
{
    if (name.empty() || name == current_dir_marker || name == parent_dir_marker) {
        error = rv_editor_text("files.name_is_needed");
        return false;
    }
    if (name.find('/') != std::string::npos || name.find('\0') != std::string::npos) {
        error = rv_editor_text("files.name_cannot_hold_slash");
        return false;
    }
    return true;
}

bool rv_editor_files::inside(const std::filesystem::path &path) const
{
    if (root_.path.empty()) {
        return false;
    }
    // weakly_canonical resolves ".." and links in what exists, so a path that
    // only looks like it is under the root is caught.
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }
    const std::string &r = root_.path.native();
    return p.native() == r || p.native().starts_with(r + path_separator);
}

int rv_editor_files::create_file(const std::filesystem::path &dir, const std::string &name, std::string &error)
{
    if (!valid_name(name, error)) {
        return RV_ERR_INVAL;
    }
    const std::filesystem::path path = dir / name;
    if (!inside(path)) {
        error = path.string() + " is outside the project";
        return RV_ERR_INVAL;
    }
    // "x": fails when the file exists, so nothing is overwritten.
    std::FILE *f = std::fopen(path.c_str(), "wx");
    if (f == nullptr) {
        int err = errno;
        error = path.string() + ": " + std::error_code(err, std::generic_category()).message();
        if (err == EEXIST) {
            return RV_ERR_INVAL;
        }
        if (err == ENOENT) {
            return RV_ERR_NOENT;
        }
        return RV_ERR_IO;
    }
    std::fclose(f);
    relist(dir);
    return RV_OK;
}

int rv_editor_files::create_dir(const std::filesystem::path &dir, const std::string &name, std::string &error)
{
    if (!valid_name(name, error)) {
        return RV_ERR_INVAL;
    }
    const std::filesystem::path path = dir / name;
    if (!inside(path)) {
        error = path.string() + " is outside the project";
        return RV_ERR_INVAL;
    }
    std::error_code ec;
    if (!std::filesystem::create_directory(path, ec)) {
        error = path.string() + ": " + (ec ? ec.message() : "already exists");
        if (!ec) {
            return RV_ERR_INVAL;
        }
        if (ec.value() == ENOENT) {
            return RV_ERR_NOENT;
        }
        return RV_ERR_IO;
    }
    relist(dir);
    return RV_OK;
}

int rv_editor_files::rename(const std::filesystem::path &from, const std::string &name, std::string &error)
{
    if (!valid_name(name, error)) {
        return RV_ERR_INVAL;
    }
    const std::filesystem::path to = from.parent_path() / name;
    if (from == root_.path || !inside(from) || !inside(to)) {
        error = rv_editor_text("files.cannot_rename_outside");
        return RV_ERR_INVAL;
    }
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::symlink_status(to, ec))) {
        error = to.string() + " already exists";
        return RV_ERR_INVAL;
    }
    std::filesystem::rename(from, to, ec);
    if (ec) {
        error = from.string() + ": " + ec.message();
        if (ec.value() == ENOENT) {
            return RV_ERR_NOENT;
        }
        return RV_ERR_IO;
    }
    relist(from.parent_path());
    return RV_OK;
}

int rv_editor_files::remove(const std::filesystem::path &path, std::string &error)
{
    // The link itself is judged, not its target: deleting a link that points
    // outside the project removes only the link.
    const std::filesystem::path parent = path.parent_path();
    std::string bad;
    if (path == root_.path || !valid_name(path.filename().string(), bad) || !inside(parent)) {
        error = rv_editor_text("files.cannot_delete_outside");
        return RV_ERR_INVAL;
    }
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
        error = path.string() + ": " + ec.message();
        if (ec.value() == ENOENT) {
            return RV_ERR_NOENT;
        }
        return RV_ERR_IO;
    }
    relist(parent);
    return RV_OK;
}

bool rv_editor_file_binary(const std::filesystem::path &path, uintmax_t &size)
{
    std::error_code ec;
    size = std::filesystem::file_size(path, ec);
    std::FILE *f = ec ? nullptr : std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    char head[file_head_read_bytes];
    const size_t n = std::fread(head, 1, sizeof(head), f);
    std::fclose(f);
    return std::find(head, head + n, '\0') != head + n;
}

} // namespace rv_editor
