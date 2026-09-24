#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace rv_editor
{

// Watches a project tree for changes made by anyone: the editor, nvim, another
// program (docs/adr/0012-platforms.md, PRJ-06). A save through a temporary file
// and rename arrives as a change to the final name. Directories named in
// `skip` (the burner's .mppcburn/, .git/) are not watched, symlinked
// directories are never entered, so a link loop cannot run the watcher away
// (NFR-07), and at most `watch_max` directories are watched (NFR-04).
class rv_editor_watch
{
public:
    static constexpr size_t watch_max = 4096;

    struct rv_editor_watch_event
    {
        std::filesystem::path path; // the file or directory that changed
        bool directory = false;
        bool removed = false;       // gone (deleted or moved away)
    };

    rv_editor_watch() = default;
    rv_editor_watch(const rv_editor_watch &) = delete;
    rv_editor_watch &operator=(const rv_editor_watch &) = delete;
    ~rv_editor_watch();

    // Watches `root` and every directory under it. False with the reason.
    bool start(const std::filesystem::path &root, std::string &error);
    void stop();

    // Appends what changed since the last call; never blocks. `overflowed` turns
    // true when the kernel dropped events or the watch limit was reached: the
    // caller should re-read everything it shows. A directory moved or deleted
    // turns it true as well: every watch is set up again, so none keeps a path
    // that no longer exists.
    void poll(std::vector<rv_editor_watch_event> &out, bool &overflowed);

    static bool skipped(const std::string &name);

private:
    void add_tree(const std::filesystem::path &dir, bool &overflowed);

    std::filesystem::path root_;
    int fd_ = -1;
    std::unordered_map<int, std::filesystem::path> dirs_;
};

} // namespace rv_editor
