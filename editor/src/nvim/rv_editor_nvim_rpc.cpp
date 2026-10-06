// nvim's msgpack-rpc channel: a reader thread in, requests out.

#include "nvim/rv_editor_nvim_rpc.hpp"

#include <cerrno>
#include <chrono>
#include <poll.h>
#include <unistd.h>

#include "pdk/rv_err.h"

#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// nvim's stderr kept for Output, at most this much between two takes.
constexpr size_t rv_editor_nvim_stderr_max = 64 * 1024;

// msgpack-rpc message types from spec (https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md)
constexpr int32_t msgpack_rpc_request_type = 0;
constexpr int32_t msgpack_rpc_response_type = 1;
constexpr int32_t msgpack_rpc_notification_type = 2;

// msgpack-rpc message array sizes
constexpr size_t msgpack_rpc_request_size = 4;      // [type, id, method, args]
constexpr size_t msgpack_rpc_response_size = 4;     // [type, id, error, result]
constexpr size_t msgpack_rpc_notification_size = 3; // [type, method, params]

// Field positions in msgpack-rpc message arrays
constexpr size_t msgpack_message_type_idx = 0;
constexpr size_t msgpack_message_id_idx = 1;
constexpr size_t msgpack_response_error_idx = 2;
constexpr size_t msgpack_response_result_idx = 3;
constexpr size_t msgpack_notification_method_idx = 1;
constexpr size_t msgpack_notification_params_idx = 2;

// Poll parameters for nvim stdout and stderr
constexpr size_t poll_fd_count = 2;
constexpr int poll_timeout_ms = 100;
constexpr size_t poll_stdout_fd_idx = 0;
constexpr size_t poll_stderr_fd_idx = 1;

// I/O buffer size for reading nvim output in chunks
constexpr size_t read_chunk_size = 65536;

// nvim shutdown parameters
constexpr int nvim_shutdown_timeout_sec = 2;
constexpr int nvim_shutdown_poll_ms = 10;

// Error message for unsupported RPC requests
constexpr std::string_view unsupported_request_error = "not supported by 3dmppc-editor";

} // namespace

rv_editor_nvim_rpc::~rv_editor_nvim_rpc()
{
    stop();
}

int rv_editor_nvim_rpc::start(const std::vector<std::string> &argv, const std::filesystem::path &cwd,
    std::string &error)
{
    stop();
    const int err = proc_.start(argv, cwd, error);
    if (err != RV_OK) {
        return err;
    }
    quit_ = false;
    eof_ = false;
    broken_.clear();
    queue_.clear();
    pending_.clear();
    thread_ = std::thread([this] { reader(); });
    return RV_OK;
}

void rv_editor_nvim_rpc::stop()
{
    if (thread_.joinable()) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        space_.notify_all();
        thread_.join();
    }
    if (proc_.running()) {
        // nvim --embed leaves when its channel closes; a hung one is killed.
        proc_.close_stdin();
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(nvim_shutdown_timeout_sec);
        while (!proc_.poll() && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(nvim_shutdown_poll_ms));
        }
    }
    // The destructor of a running process kills and reaps it.
}

void rv_editor_nvim_rpc::reader()
{
    std::string buf;
    char chunk[read_chunk_size];
    const int out = proc_.stdout_fd();
    const int err = proc_.stderr_fd();
    for (;;) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (quit_) {
                return;
            }
        }
        pollfd fds[poll_fd_count] = { { out, POLLIN, 0 }, { err, POLLIN, 0 } };
        if (::poll(fds, poll_fd_count, poll_timeout_ms) <= 0) {
            continue;
        }
        if (fds[poll_stderr_fd_idx].revents & (POLLIN | POLLHUP)) {
            const ssize_t n = ::read(err, chunk, sizeof(chunk));
            if (n > 0) {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stderr_.size() < rv_editor_nvim_stderr_max) {
                    stderr_.append(chunk, static_cast<size_t>(n));
                }
            }
        }
        if (!(fds[poll_stdout_fd_idx].revents & (POLLIN | POLLHUP))) {
            continue;
        }
        const ssize_t n = ::read(out, chunk, sizeof(chunk));
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }
        if (n <= 0) {
            const std::lock_guard<std::mutex> lock(mutex_);
            eof_ = true;
            return;
        }
        buf.append(chunk, static_cast<size_t>(n));

        size_t used = 0;
        for (;;) {
            rv_editor_mpack msg;
            const ptrdiff_t r = rv_editor_mpack_read(std::string_view(buf).substr(used), msg);
            if (r == 0) {
                break;
            }
            std::unique_lock<std::mutex> lock(mutex_);
            if (r < 0) {
                broken_ = rv_editor_text("nvim_rpc.sent_not_msgpack");
                eof_ = true;
                return;
            }
            used += static_cast<size_t>(r);
            // Full: wait for the UI thread to take some, and nvim waits with us.
            space_.wait(lock, [this] { return quit_ || queue_.size() < queue_max; });
            if (quit_) {
                return;
            }
            queue_.push_back(std::move(msg));
        }
        buf.erase(0, used);
    }
}

void rv_editor_nvim_rpc::request(const std::string &method, const std::string &args, rv_editor_nvim_reply reply)
{
    const uint32_t id = next_id_++;
    std::string out;
    rv_editor_mpack_writer w(out);
    w.array(msgpack_rpc_request_size);
    w.integer(msgpack_rpc_request_type);
    w.integer(id);
    w.string(method);
    out += args;
    if (proc_.write(out) != RV_OK) {
        // Never sent, so never answered: the caller hears it now.
        if (reply) {
            rv_editor_mpack error;
            error.type = rv_editor_mpack::rv_editor_mpack_type::string;
            if (proc_.stdin_open()) {
                error.s = rv_editor_text("nvim_rpc.not_reading_input");
            } else {
                error.s = rv_editor_text("nvim_rpc.input_closed");
            }
            reply(error, rv_editor_mpack{});
        }
        return;
    }
    pending_[id] = std::move(reply);
}

void rv_editor_nvim_rpc::notify(const std::string &method, const std::string &args)
{
    std::string out;
    rv_editor_mpack_writer w(out);
    w.array(msgpack_rpc_notification_size);
    w.integer(msgpack_rpc_notification_type);
    w.string(method);
    out += args;
    (void)proc_.write(out);
}

bool rv_editor_nvim_rpc::poll(const rv_editor_nvim_notify &on_notify, std::string &why)
{
    proc_.flush();
    std::deque<rv_editor_mpack> msgs;
    bool eof = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        msgs.swap(queue_);
        eof = eof_;
        if (!broken_.empty()) {
            why = broken_;
        }
    }
    space_.notify_all();

    using mtype = rv_editor_mpack::rv_editor_mpack_type;
    for (const rv_editor_mpack &m : msgs) {
        if (!m.is(mtype::array) || m.items.empty()) {
            continue;
        }
        const int64_t kind = m.items[msgpack_message_type_idx].i;
        if (kind == msgpack_rpc_response_type && m.items.size() == msgpack_rpc_response_size) {
            const auto it = pending_.find(static_cast<uint32_t>(m.items[msgpack_message_id_idx].i));
            if (it != pending_.end()) {
                rv_editor_nvim_reply reply = std::move(it->second);
                pending_.erase(it);
                if (reply) {
                    reply(m.items[msgpack_response_error_idx], m.items[msgpack_response_result_idx]);
                }
            }
        } else if (kind == msgpack_rpc_notification_type && m.items.size() == msgpack_rpc_notification_size) {
            on_notify(m.items[msgpack_notification_method_idx].s, m.items[msgpack_notification_params_idx]);
        } else if (kind == msgpack_rpc_request_type && m.items.size() == msgpack_rpc_request_size) {
            // nvim asking the UI something: this client answers nothing.
            std::string out;
            rv_editor_mpack_writer w(out);
            w.array(msgpack_rpc_response_size);
            w.integer(msgpack_rpc_response_type);
            w.integer(m.items[msgpack_message_id_idx].i);
            w.string(unsupported_request_error);
            w.nil();
            (void)proc_.write(out);
        }
    }

    if (eof || !proc_.running()) {
        proc_.poll();
        if (why.empty()) {
            if (proc_.exit_status().exited) {
                const std::string exit_info = rv_editor_exit_text(proc_.exit_status());
                why = rv_editor_text_format("nvim_rpc.ended",
                    std::make_format_args(exit_info));
            } else {
                why = rv_editor_text("nvim_rpc.closed_channel");
            }
        }
        return false;
    }
    return true;
}

std::string rv_editor_nvim_rpc::take_stderr()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::string s;
    s.swap(stderr_);
    return s;
}

} // namespace rv_editor
