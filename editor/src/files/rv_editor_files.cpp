// The project tree: lazy listing, watcher-driven refresh, confined file operations.

#include "files/rv_editor_files.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <map>
#include <system_error>

namespace rv_editor
{

void rv_editor_files::open(const std::filesystem::path &root, rv_editor_log &log)
{
    close();
    // Canonical, so inside() compares like with like.
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(root, ec);
    root_.path = ec ? root : canonical;
    root_.name = root_.path.filename().string();
    root_.dir = true;
    root_.expanded = true;
    std::string error;
    watching_ = watch_.start(root_.path, error);
    if (!watching_) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
            "files are not watched, Refresh shows changes: " + error);
    }
    list(root_);
}

void rv_editor_files::close()
{
    watch_.stop();
    watching_ = false;
    root_ = {};
    stale_.clear();
    changed.clear();
    selected.clear();
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
        if (child.dir && !child.symlink && path.native().starts_with(child.path.native() + "/")) {
            return find(child, path);
        }
        if (child.path == path) {
            return &child;
        }
    }
    return nullptr;
}

void rv_editor_files::update(rv_editor_log &log)
{
    if (!is_open() || !watching_) {
        return;
    }
    std::vector<rv_editor_watch::rv_editor_watch_event> events;
    bool overflowed = false;
    watch_.poll(events, overflowed);
    for (const auto &ev : events) {
        stale_.insert(ev.path.parent_path());
        if (!ev.directory) {
            changed.push_back(ev.path);
        }
    }
    if (overflowed) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
            "the file watcher lost track of changes; the whole tree is re-read");
        stale_.clear();
        list(root_);
        return;
    }
    for (const std::filesystem::path &dir : stale_) {
        relist(dir);
    }
    stale_.clear();
    // Bounded: nobody who needs this list lets it grow across frames.
    if (changed.size() > 4096) {
        changed.erase(changed.begin(), changed.end() - 4096);
    }
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
    if (name.empty() || name == "." || name == "..") {
        error = "a name is needed";
        return false;
    }
    if (name.find('/') != std::string::npos || name.find('\0') != std::string::npos) {
        error = "a name cannot hold '/'";
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
    return p.native() == r || p.native().starts_with(r + "/");
}

bool rv_editor_files::create_file(const std::filesystem::path &dir, const std::string &name, std::string &error)
{
    if (!valid_name(name, error)) {
        return false;
    }
    const std::filesystem::path path = dir / name;
    if (!inside(path)) {
        error = path.string() + " is outside the project";
        return false;
    }
    // "x": fails when the file exists, so nothing is overwritten.
    std::FILE *f = std::fopen(path.c_str(), "wx");
    if (f == nullptr) {
        error = path.string() + ": " + std::error_code(errno, std::generic_category()).message();
        return false;
    }
    std::fclose(f);
    relist(dir);
    return true;
}

bool rv_editor_files::create_dir(const std::filesystem::path &dir, const std::string &name, std::string &error)
{
    if (!valid_name(name, error)) {
        return false;
    }
    const std::filesystem::path path = dir / name;
    if (!inside(path)) {
        error = path.string() + " is outside the project";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::create_directory(path, ec)) {
        error = path.string() + ": " + (ec ? ec.message() : "already exists");
        return false;
    }
    relist(dir);
    return true;
}

bool rv_editor_files::rename(const std::filesystem::path &from, const std::string &name, std::string &error)
{
    if (!valid_name(name, error)) {
        return false;
    }
    const std::filesystem::path to = from.parent_path() / name;
    if (from == root_.path || !inside(from) || !inside(to)) {
        error = "only files and directories inside the project can be renamed";
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::symlink_status(to, ec))) {
        error = to.string() + " already exists";
        return false;
    }
    std::filesystem::rename(from, to, ec);
    if (ec) {
        error = from.string() + ": " + ec.message();
        return false;
    }
    relist(from.parent_path());
    if (selected == from) {
        selected = to;
    }
    return true;
}

bool rv_editor_files::remove(const std::filesystem::path &path, std::string &error)
{
    // The link itself is judged, not its target: deleting a link that points
    // outside the project removes only the link.
    const std::filesystem::path parent = path.parent_path();
    std::string bad;
    if (path == root_.path || !valid_name(path.filename().string(), bad) || !inside(parent)) {
        error = "only files and directories inside the project can be deleted";
        return false;
    }
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
        error = path.string() + ": " + ec.message();
        return false;
    }
    relist(parent);
    if (selected == path) {
        selected.clear();
    }
    return true;
}

} // namespace rv_editor
