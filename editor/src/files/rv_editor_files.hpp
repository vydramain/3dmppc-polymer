#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "log/rv_editor_log.hpp"
#include "platform/rv_editor_watch.hpp"

namespace rv_editor
{

// One entry of the project tree as the disk has it.
struct rv_editor_file_node
{
    std::string name;
    std::filesystem::path path;
    bool dir = false;
    bool symlink = false;  // shown, never followed (NFR-07)
    bool listed = false;   // children read from disk
    bool expanded = false; // open in the Files tree
    std::vector<rv_editor_file_node> children; // directories first, then by name
};

// The Files model: the project directory, read one directory at a time as the
// tree opens it (NFR-04) and kept current by the watcher, whoever changed the
// disk (PRJ-06). Every operation stays inside the project root and never
// replaces an existing file without being asked (NFR-07, TPL-02).
class rv_editor_files
{
public:
    void open(const std::filesystem::path &root, rv_editor_log &log);
    void close();
    bool is_open() const { return !root_.path.empty(); }

    // Once a frame: applies what the watcher saw. Changed files are added to
    // `changed` for whoever needs them (disc.toml, open documents).
    void update(rv_editor_log &log);

    rv_editor_file_node &root() { return root_; }
    // Reads a directory's entries now, keeping what was expanded under it.
    void list(rv_editor_file_node &dir);
    void refresh();
    // Selects `path` and opens the directories down to it, reading them when
    // needed; the rest of the tree keeps what was open. Nothing outside the root.
    void reveal(const std::filesystem::path &path);

    // Name checks shared by every operation: one path component, no "/", no NUL.
    static bool valid_name(const std::string &name, std::string &error);
    // True when `path` is the root or lies under it, after resolving "..".
    bool inside(const std::filesystem::path &path) const;

    bool create_file(const std::filesystem::path &dir, const std::string &name, std::string &error);
    bool create_dir(const std::filesystem::path &dir, const std::string &name, std::string &error);
    bool rename(const std::filesystem::path &from, const std::string &name, std::string &error);
    // Deletes a file, a symlink (not what it points to) or a whole directory.
    bool remove(const std::filesystem::path &path, std::string &error);

    std::filesystem::path selected;
    std::vector<std::filesystem::path> changed;

private:
    rv_editor_file_node *find(rv_editor_file_node &node, const std::filesystem::path &path);
    void relist(const std::filesystem::path &dir);

    rv_editor_file_node root_;
    rv_editor_watch watch_;
    std::set<std::filesystem::path> stale_;
    bool watching_ = false;
};

} // namespace rv_editor
