// nvim's msgpack-rpc channel: a reader thread in, requests out.

#include "nvim/rv_editor_nvim_rpc.hpp"

#include <cerrno>
#include <chrono>
#include <poll.h>
#include <unistd.h>

namespace rv_editor
{

namespace
{

// nvim's stderr kept for Output, at most this much between two takes.
constexpr size_t rv_editor_nvim_stderr_max = 64 * 1024;

} // namespace

rv_editor_nvim_rpc::~rv_editor_nvim_rpc()
{
    stop();
}

bool rv_editor_nvim_rpc::start(const std::vector<std::string> &argv, const std::filesystem::path &cwd,
    std::string &error)
{
    stop();
    if (!proc_.start(argv, cwd, error)) {
        return false;
    }
    quit_ = false;
    eof_ = false;
    broken_.clear();
    queue_.clear();
    pending_.clear();
    thread_ = std::thread([this] { reader(); });
    return true;
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
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!proc_.poll() && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    // The destructor of a running process kills and reaps it.
}

void rv_editor_nvim_rpc::reader()
{
    std::string buf;
    char chunk[65536];
    const int out = proc_.stdout_fd();
    const int err = proc_.stderr_fd();
    for (;;) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (quit_) {
                return;
            }
        }
        pollfd fds[2] = { { out, POLLIN, 0 }, { err, POLLIN, 0 } };
        if (::poll(fds, 2, 100) <= 0) {
            continue;
        }
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            const ssize_t n = ::read(err, chunk, sizeof(chunk));
            if (n > 0) {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stderr_.size() < rv_editor_nvim_stderr_max) {
                    stderr_.append(chunk, static_cast<size_t>(n));
                }
            }
        }
        if (!(fds[0].revents & (POLLIN | POLLHUP))) {
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
                broken_ = "nvim sent something that is not msgpack";
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
    w.array(4);
    w.integer(0);
    w.integer(id);
    w.string(method);
    out += args;
    pending_[id] = std::move(reply);
    proc_.write(out);
}

void rv_editor_nvim_rpc::notify(const std::string &method, const std::string &args)
{
    std::string out;
    rv_editor_mpack_writer w(out);
    w.array(3);
    w.integer(2);
    w.string(method);
    out += args;
    proc_.write(out);
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
        const int64_t kind = m.items[0].i;
        if (kind == 1 && m.items.size() == 4) {
            const auto it = pending_.find(static_cast<uint32_t>(m.items[1].i));
            if (it != pending_.end()) {
                rv_editor_nvim_reply reply = std::move(it->second);
                pending_.erase(it);
                if (reply) {
                    reply(m.items[2], m.items[3]);
                }
            }
        } else if (kind == 2 && m.items.size() == 3) {
            on_notify(m.items[1].s, m.items[2]);
        } else if (kind == 0 && m.items.size() == 4) {
            // nvim asking the UI something: this client answers nothing.
            std::string out;
            rv_editor_mpack_writer w(out);
            w.array(4);
            w.integer(1);
            w.integer(m.items[1].i);
            w.string("not supported by 3dmppc-editor");
            w.nil();
            proc_.write(out);
        }
    }

    if (eof || !proc_.running()) {
        proc_.poll();
        if (why.empty()) {
            why = proc_.exit_status().exited ? "nvim ended: " + rv_editor_exit_text(proc_.exit_status())
                                             : "nvim closed its channel";
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
