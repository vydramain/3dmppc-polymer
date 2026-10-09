#pragma once

#include "log/rv_editor_log.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rv_editor
{

// Writes the process lines of chosen pids to files on a thread of its own.
// The frame calls open/feed/poll/path_for and only queues; the writer thread
// creates directories, opens and writes every file, and owns each FILE.
// The frame-side methods are not thread-safe among themselves.
class rv_editor_log_file
{
public:
    // Files open at once; a new open past this closes the oldest.
    static constexpr size_t max_files = 8;

    // Queues opening `path` for `pid` and the lines already in `log` for it. The old file for
    // `pid` is closed first, in queue order. The directory and file are made on the writer
    // thread, so their failure comes back through poll. RV_ERR_INVAL with the reason in error
    // for an empty path or a path already written for another pid.
    // Call feed() every frame before the channel's clear(): lines added between the last feed
    // and a clear never reach a file.
    int open(int64_t pid, const std::filesystem::path &path, const rv_editor_log &log, std::string &error);
    // Queues the lines of `log` newer than the last ones read, for the open files. Lines the
    // channel evicted before this read are marked in every open file. Call it every frame,
    // before the channel's clear().
    void feed(const rv_editor_log &log);
    // Reports one failure not yet reported (RV_ERR_IO, "<path>: <reason>"), else RV_OK.
    int poll(std::string &error);
    // Path of the file open for `pid`, empty when none. Named until poll reports that file's failure.
    std::string path_for(int64_t pid) const;

    rv_editor_log_file();
    rv_editor_log_file(const rv_editor_log_file &) = delete;
    rv_editor_log_file &operator=(const rv_editor_log_file &) = delete;
    // Waits for the queue to drain, then closes the files.
    ~rv_editor_log_file();

private:
    // Frame-thread view of an open file. `key` is the normalized path, for the same-file check;
    // `last_seq` is the newest log line read for it.
    struct file_entry {
        int64_t pid;
        uint64_t id;
        std::string path;
        std::string key;
        uint64_t last_seq;
    };

    struct job {
        enum class kind : uint8_t {
            attach,
            text,
            detach,
        };
        kind what;
        uint64_t id = 0;
        std::string path; // attach only
        std::string line; // text only
    };

    struct failure {
        uint64_t id;
        std::string message;
    };

    struct open_file {
        FILE *handle;
        std::string path;
    };

    size_t index_of(int64_t pid) const;
    void close_at(size_t index);
    void queue_line(uint64_t id, const rv_editor_log_line &line);
    void queue_text(uint64_t id, std::string text);
    void push(job &&item);
    bool take_failure(uint64_t &id, std::string &message);

    void run();
    void apply(std::map<uint64_t, open_file> &files, job &item);
    void attach(std::map<uint64_t, open_file> &files, const job &item);
    void fail(uint64_t id, std::string message);

    std::vector<file_entry> files_;
    uint64_t next_id_ = 1;
    uint64_t dropped_seen_ = 0; // log.dropped() at the last feed

    std::mutex mutex_;
    std::condition_variable wake_;
    // ponytail: unbounded queue; add a cap if a hung disk is ever seen.
    std::deque<job> jobs_;         // guarded by mutex_
    std::deque<failure> failures_; // guarded by mutex_
    bool stopping_ = false;        // guarded by mutex_
    // Last member: the writer starts in the constructor and must see the others initialized.
    std::thread writer_;
};

} // namespace rv_editor
