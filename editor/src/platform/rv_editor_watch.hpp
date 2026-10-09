#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace rv_editor
{

// Watches a project tree for changes made by anyone: the editor, nvim, another
// program. A save through a temporary file
// and rename arrives as a change to the final name. Directories named in
// `skip` (the burner's .mppcburn/, .git/) are not watched, symlinked
// directories are never entered, so a link loop cannot run the watcher away,
// and at most `watch_max` directories are watched.
// Operations return RV_OK or an RV code with the reason in `error`; restarting
// is stop() followed by start(), done by the caller.
class rv_editor_watch
{
public:
    static constexpr size_t watch_max = 4096;

    struct rv_editor_watch_event {
        std::filesystem::path path; // the file or directory that changed
        bool directory = false;
        bool removed = false;  // gone (deleted or moved away)
        bool appeared = false; // directory created or moved in: the caller calls watch_tree() on it
        bool lost = false;     // the kernel dropped events: re-read everything, path is empty
    };

    rv_editor_watch() = default;
    rv_editor_watch(const rv_editor_watch &) = delete;
    rv_editor_watch &operator=(const rv_editor_watch &) = delete;
    ~rv_editor_watch();

    // Opens the inotify descriptor and watches `root` and every directory under it.
    // RV_ERR_INVAL if already started. RV_ERR_NOMEM: the watch limit ran out, the part reached so far stays watched.
    // Restart is stop() then start(), done by the caller.
    int start(const std::filesystem::path &root, std::string &error);
    // Drops every watch and closes the descriptor; the watch list is cleared even on failure.
    // RV_ERR_ALREADY if not started. RV_ERR_IO: close() failed, the descriptor is dropped anyway.
    // `error` is set for both codes.
    int stop(std::string &error);
    // Watches `dir` and everything under it; RV_ERR_NOENT when `dir` is already gone.
    int watch_tree(const std::filesystem::path &dir, std::string &error);

    // Appends the raw inotify records that are queued now; never blocks.
    int read_events(std::vector<char> &raw, std::string &error);
    // Turns raw records into events; never fails.
    void translate(const std::vector<char> &raw, std::vector<rv_editor_watch_event> &out);

    static bool skipped(const std::string &name);

private:
    int watch_dir(const std::filesystem::path &dir, std::string &error);
    int add_tree(const std::filesystem::path &dir, std::string &error);
    static int subdirs(const std::filesystem::path &dir, std::vector<std::filesystem::path> &out, std::string &error);

    int fd_ = -1;
    std::unordered_map<int, std::filesystem::path> dirs_;
};

} // namespace rv_editor
