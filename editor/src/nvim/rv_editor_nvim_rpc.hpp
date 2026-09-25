#pragma once

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

// `nvim --embed` and its msgpack-rpc channel (docs/adr/0005-code-editor-nvim.md).
// A reader thread decodes stdout into a bounded queue; when the queue is full it
// waits, so nvim waits too and nothing is dropped. The UI thread sends requests
// and handles responses and notifications once a frame (DEV-05).
class rv_editor_nvim_rpc
{
public:
    using rv_editor_nvim_reply = std::function<void(const rv_editor_mpack &error, const rv_editor_mpack &result)>;
    using rv_editor_nvim_notify = std::function<void(const std::string &method, const rv_editor_mpack &params)>;

    static constexpr size_t queue_max = 8192;

    ~rv_editor_nvim_rpc();

    // argv[0] is the nvim executable. False with the reason.
    bool start(const std::vector<std::string> &argv, const std::filesystem::path &cwd, std::string &error);
    // Ends nvim: closes its input, waits a moment, then kills it.
    void stop();
    bool running() const { return proc_.running(); }

    // Sends `method(args...)`; `args` must be one encoded msgpack array.
    // `reply` runs on the UI thread in poll(); it may be empty.
    void request(const std::string &method, const std::string &args, rv_editor_nvim_reply reply = {});
    void notify(const std::string &method, const std::string &args);

    // Handles what arrived: responses to their callbacks, notifications to
    // `on_notify`. Returns false once nvim has ended; `why` then says how.
    bool poll(const rv_editor_nvim_notify &on_notify, std::string &why);

    // nvim's own stderr, for Output.
    std::string take_stderr();

private:
    void reader();

    rv_editor_process proc_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable space_;
    std::deque<rv_editor_mpack> queue_;
    std::string stderr_;
    bool quit_ = false;
    bool eof_ = false;
    std::string broken_; // the stream stopped being msgpack
    uint32_t next_id_ = 1;
    std::map<uint32_t, rv_editor_nvim_reply> pending_;
};

} // namespace rv_editor
