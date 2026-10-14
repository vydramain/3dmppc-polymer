// nvim's msgpack-rpc channel: a reader thread in, requests out.

#include "nvim/rv_editor_nvim_rpc.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
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

// nvim shutdown: the deadline to leave after its input closed, and the poll step of
// stop_now at the editor's exit (no frame runs then).
constexpr int nvim_shutdown_timeout_sec = 2;
constexpr int nvim_shutdown_poll_ms = 10;
// After SIGKILL: how long nvim may take to exit, polled by waitpid, before it counts as not ended.
constexpr int nvim_kill_reap_ms = 500;

// Error message for unsupported RPC requests
constexpr std::string_view unsupported_request_error = "not supported by 3dmppc-editor";

} // namespace

rv_editor_nvim_rpc::~rv_editor_nvim_rpc()
{
    // Nothing to report to in a destructor; a child still running is killed by its process.
    std::string error;
    stop_now(error);
}

int rv_editor_nvim_rpc::start(const std::vector<std::string> &argv, const std::filesystem::path &cwd, std::string &error)
{
    if (stopping_) {
        error = "nvim is still stopping";
        return RV_ERR_BUSY;
    }
    if (thread_.joinable()) {
        error = "nvim is already running";
        return RV_ERR_ALREADY;
    }
    const int err = proc_.start(argv, cwd, error);
    if (err != RV_OK) {
        return err;
    }
    quit_ = false;
    eof_ = false;
    reader_done_ = false;
    broken_.clear();
    queue_.clear();
    pending_.clear();
    thread_ = std::thread([this] {
        reader();
        const std::lock_guard<std::mutex> lock(mutex_);
        reader_done_ = true;
    });
    return RV_OK;
}

int rv_editor_nvim_rpc::begin_stop(std::string &error)
{
    if (stopping_) {
        error = "nvim is already stopping";
        return RV_ERR_ALREADY;
    }
    if (!proc_.running() && !thread_.joinable()) {
        return RV_OK;
    }
    stopping_ = true;
    stop_failure_reported_ = false;
    kill_sent_ = false;
    stop_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(nvim_shutdown_timeout_sec);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    space_.notify_all();
    // nvim --embed leaves when its channel closes; a hung one is killed at the deadline.
    proc_.close_stdin();
    return RV_OK;
}

int rv_editor_nvim_rpc::send_kill(std::string &error)
{
    // errno belongs to the failed kill: read it before anything else runs.
    const int rc = proc_.stop(true);
    const int cause = errno;
    if (rc == RV_ERR_IO) {
        error = std::string("kill: ") + std::strerror(cause);
        return RV_ERR_IO;
    }
    kill_sent_ = true;
    stop_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(nvim_kill_reap_ms);
    return RV_OK;
}

void rv_editor_nvim_rpc::wait_for_exit()
{
    // No frame runs here (stop_now at the editor's exit): a short sleep between polls.
    while (proc_.running() && std::chrono::steady_clock::now() < stop_until_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(nvim_shutdown_poll_ms));
        proc_.poll();
    }
}

int rv_editor_nvim_rpc::stop_step(std::string &error)
{
    if (!stopping_) {
        return RV_OK;
    }
    proc_.poll();
    int rc = RV_OK;
    const bool overdue = proc_.running() && std::chrono::steady_clock::now() >= stop_until_;
    if (overdue && !kill_sent_) {
        // Past the deadline: SIGKILL to the process group; a failed kill is retried next frame.
        rc = send_kill(error);
    } else if (overdue) {
        error = "nvim not ended after SIGKILL";
        rc = RV_ERR_IO;
    }
    bool reader_done = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        reader_done = reader_done_;
    }
    if (reader_done && thread_.joinable()) {
        thread_.join();
    }
    if (!proc_.running() && !thread_.joinable()) {
        stopping_ = false;
    }
    // A failure persists until the stop ends: reported once, not on every frame.
    if (rc != RV_OK && !stop_failure_reported_) {
        stop_failure_reported_ = true;
        return rc;
    }
    return RV_OK;
}

int rv_editor_nvim_rpc::stop_now(std::string &error)
{
    int rc = stopping_ ? RV_OK : begin_stop(error);
    wait_for_exit();
    const bool kill_now = proc_.running() && !kill_sent_;
    const int kill_rc = kill_now ? send_kill(error) : RV_OK;
    if (kill_rc != RV_OK) {
        rc = kill_rc;
    }
    wait_for_exit();
    if (proc_.running()) {
        error += error.empty() ? "nvim not ended after SIGKILL" : "; nvim not ended after SIGKILL";
        rc = RV_ERR_IO;
    }
    // The reader leaves once quit_ is set, or when nvim's output ends with the kill.
    if (thread_.joinable()) {
        thread_.join();
    }
    stopping_ = proc_.running();
    if (rc != RV_OK) {
        return rc;
    }
    return RV_OK;
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
            space_.wait(lock, [this] {
                return quit_ || queue_.size() < queue_max;
            });
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

int rv_editor_nvim_rpc::notify(const std::string &method, const std::string &args)
{
    std::string out;
    rv_editor_mpack_writer w(out);
    w.array(msgpack_rpc_notification_size);
    w.integer(msgpack_rpc_notification_type);
    w.string(method);
    out += args;
    return proc_.write(out);
}

void rv_editor_nvim_rpc::handle_response(const rv_editor_mpack &m)
{
    if (m.items.size() != msgpack_rpc_response_size) {
        return;
    }
    const auto it = pending_.find(static_cast<uint32_t>(m.items[msgpack_message_id_idx].i));
    if (it == pending_.end()) {
        return;
    }
    rv_editor_nvim_reply reply = std::move(it->second);
    pending_.erase(it);
    if (reply) {
        reply(m.items[msgpack_response_error_idx], m.items[msgpack_response_result_idx]);
    }
}

void rv_editor_nvim_rpc::handle_notification(const rv_editor_mpack &m, const rv_editor_nvim_notify &on_notify)
{
    if (m.items.size() != msgpack_rpc_notification_size) {
        return;
    }
    on_notify(m.items[msgpack_notification_method_idx].s, m.items[msgpack_notification_params_idx]);
}

void rv_editor_nvim_rpc::handle_request(const rv_editor_mpack &m)
{
    if (m.items.size() != msgpack_rpc_request_size) {
        return;
    }
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
        switch (kind) {
        case msgpack_rpc_response_type:
            handle_response(m);
            break;
        case msgpack_rpc_notification_type:
            handle_notification(m, on_notify);
            break;
        case msgpack_rpc_request_type:
            handle_request(m);
            break;
        default:
            // msgpack-RPC has only these three message types; anything else is not for this client.
            break;
        }
    }

    if (eof || !proc_.running()) {
        proc_.poll();
        if (why.empty()) {
            if (proc_.exit_status().exited) {
                const std::string exit_info = rv_editor_exit_text(proc_.exit_status());
                why = rv_editor_text_format("nvim_rpc.ended", std::make_format_args(exit_info));
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
