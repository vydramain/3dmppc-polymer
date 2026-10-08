#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nvim/rv_editor_msgpack.hpp"
#include "platform/rv_editor_process.hpp"

namespace rv_editor
{

// `nvim --embed` and its msgpack-rpc channel.
// A reader thread decodes stdout into a bounded queue; when the queue is full it
// waits, so nvim waits too and nothing is dropped. The UI thread sends requests
// and handles responses and notifications once a frame.
class rv_editor_nvim_rpc
{
public:
    using rv_editor_nvim_reply = std::function<void(const rv_editor_mpack &error, const rv_editor_mpack &result)>;
    using rv_editor_nvim_notify = std::function<void(const std::string &method, const rv_editor_mpack &params)>;

    static constexpr size_t queue_max = 8192;

    ~rv_editor_nvim_rpc();

    // argv[0] is the nvim executable. Returns RV_OK or RV_ERR_* with the reason in error;
    // RV_ERR_BUSY while a stop runs.
    int start(const std::vector<std::string> &argv, const std::filesystem::path &cwd, std::string &error);
    bool running() const
    {
        return proc_.running();
    }

    // Ending nvim, as rv_editor_pty does. begin_stop closes its input and sets a deadline,
    // then returns. stop_step runs once a frame while stopping() and never waits: it reaps,
    // SIGKILLs past the deadline and joins the reader once that has ended. stop_now, for the
    // editor's exit only, waits at most the deadline, then the SIGKILL reap wait, then joins
    // the reader (it leaves once quit_ is set). RV_ERR_ALREADY, or RV_ERR_IO with the reason
    // in `error`.
    int begin_stop(std::string &error);
    bool stopping() const
    {
        return stopping_;
    }
    int stop_step(std::string &error);
    int stop_now(std::string &error);

    // Sends `method(args...)`; `args` must be one encoded msgpack array.
    // `reply` runs on the UI thread in poll(); it may be empty.
    void request(const std::string &method, const std::string &args, rv_editor_nvim_reply reply = {});
    // Returns RV_OK or RV_ERR_* (RV_ERR_IO if write fails).
    int notify(const std::string &method, const std::string &args);

    // Handles what arrived: responses to their callbacks, notifications to
    // `on_notify`. Returns false once nvim has ended; `why` then says how.
    bool poll(const rv_editor_nvim_notify &on_notify, std::string &why);

    // nvim's own stderr, for Output.
    std::string take_stderr();

private:
    void reader();
    // SIGKILL to the process group; sets the reap deadline. RV_OK, or RV_ERR_IO with the reason.
    int send_kill(std::string &error);
    void wait_for_exit();
    void handle_response(const rv_editor_mpack &m);
    void handle_notification(const rv_editor_mpack &m, const rv_editor_nvim_notify &on_notify);
    void handle_request(const rv_editor_mpack &m);

    rv_editor_process proc_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable space_;
    std::deque<rv_editor_mpack> queue_;
    std::string stderr_;
    bool quit_ = false;
    bool eof_ = false;
    bool reader_done_ = false; // reader() has returned, so its thread may be joined
    std::string broken_; // the stream stopped being msgpack
    uint32_t next_id_ = 1;
    std::map<uint32_t, rv_editor_nvim_reply> pending_;
    bool stopping_ = false;
    bool stop_failure_reported_ = false; // a failing stop step is reported once per stop
    bool kill_sent_ = false;             // SIGKILL past the deadline is sent once per stop
    std::chrono::steady_clock::time_point stop_until_{};
};

} // namespace rv_editor
