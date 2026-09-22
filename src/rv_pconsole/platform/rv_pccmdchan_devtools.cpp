// Framing, buffering and the two POSIX facts this channel rests on: a
// descriptor can be put into non-blocking mode, and SIGPIPE can be refused.
//
// The read side is ONE growable buffer with a consumed_ offset rather than an
// erase-from-the-front queue: a 4 MiB payload would otherwise be memmoved on
// every partial arrival. Compaction happens when the buffer drains or the dead
// prefix grows past a threshold, which is the only point where copying is worth
// it.
#include "rv_pconsole/platform/rv_pccmdchan_devtools.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <thread>

#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/platform/rv_pccmdhex.hpp"

namespace rv_3dmppc
{

namespace
{

// One read() per iteration is not enough (a payload would take thousands of
// frames) and an unbounded loop is not acceptable either (a fast sender would
// own the frame). This is the compromise: as much as this per service call.
constexpr std::size_t RV_PCCMDCHAN_READ_CHUNK = 64 * 1024;
constexpr std::size_t RV_PCCMDCHAN_READ_PER_TICK = 1 << 20;

// Above this many dead bytes at the front, copying the live tail down is
// cheaper than carrying the corpse.
constexpr std::size_t RV_PCCMDCHAN_COMPACT_AT = 64 * 1024;

struct sigaction rv_pccmdchan_sigpipe_old;

// A token is a run of non-space characters; runs of spaces collapse. An editor
// that lines its arguments up with two spaces is not a protocol error.
std::vector<std::string> split_tokens(std::string_view line)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ') {
            ++i;
        }
        if (i > start) {
            out.emplace_back(line.substr(start, i - start));
        }
    }
    return out;
}

// Digits only, and the result must fit. strtoll would accept "+3", " 3" and
// "0x3", none of which this protocol has any business allowing: a size read
// loosely is a size an attacker picks.
bool parse_u63(std::string_view text, int64_t &out)
{
    if (text.empty() || text.size() > 18) {
        return false;
    }
    int64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

} // namespace

rv_pccmdchan_stdio::rv_pccmdchan_stdio()
{
    // SIGPIPE first. Without this, the first write to a closed pipe kills the
    // process from underneath the frame loop - no disc_shutdown, no loader
    // teardown, no log line. With it, the same event is an EPIPE that close()
    // below turns into an ordinary disconnect.
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    ignore.sa_flags = 0;
    sigpipe_saved_ = ::sigaction(SIGPIPE, &ignore, &rv_pccmdchan_sigpipe_old) == 0;

    in_flags_ = ::fcntl(STDIN_FILENO, F_GETFL);
    out_flags_ = ::fcntl(STDOUT_FILENO, F_GETFL);
    if (in_flags_ < 0 || out_flags_ < 0 ||
        ::fcntl(STDIN_FILENO, F_SETFL, in_flags_ | O_NONBLOCK) < 0 ||
        ::fcntl(STDOUT_FILENO, F_SETFL, out_flags_ | O_NONBLOCK) < 0) {
        RV_LOG_ERR("pccmd", "cannot put stdin/stdout into non-blocking mode; dev channel is down");
        connected_ = false;
        reason_ = "stdin/stdout cannot be made non-blocking";
        return;
    }
    RV_LOG_INFO("pccmd", "development channel open on stdin/stdout (protocol 1)");
}

rv_pccmdchan_stdio::~rv_pccmdchan_stdio()
{
    // Flags go back even if the channel died early: they belong to the process,
    // not to this object, and a shell left with a non-blocking stdin is a shell
    // that starts failing reads for reasons nobody will connect to us.
    if (in_flags_ >= 0) {
        ::fcntl(STDIN_FILENO, F_SETFL, in_flags_);
    }
    if (out_flags_ >= 0) {
        ::fcntl(STDOUT_FILENO, F_SETFL, out_flags_);
    }
    if (sigpipe_saved_) {
        ::sigaction(SIGPIPE, &rv_pccmdchan_sigpipe_old, nullptr);
    }
}

void rv_pccmdchan_stdio::close(const char *why)
{
    if (!connected_) {
        return;
    }
    connected_ = false;
    reason_ = why;
    in_.clear();
    consumed_ = 0;
    scanned_ = 0;
    phase_ = phase::header;
    pending_ = rv_pccmdreq{};
}

void rv_pccmdchan_stdio::compact()
{
    if (consumed_ == 0) {
        return;
    }
    if (consumed_ == in_.size()) {
        in_.clear();
        scanned_ = 0;
        consumed_ = 0;
        return;
    }
    if (consumed_ < RV_PCCMDCHAN_COMPACT_AT) {
        return;
    }
    in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(consumed_));
    scanned_ = scanned_ > consumed_ ? scanned_ - consumed_ : 0;
    consumed_ = 0;
}

void rv_pccmdchan_stdio::pump_in()
{
    std::size_t taken = 0;
    while (connected_ && taken < RV_PCCMDCHAN_READ_PER_TICK) {
        const std::size_t old_size = in_.size();
        if (old_size + RV_PCCMDCHAN_READ_CHUNK > static_cast<std::size_t>(RV_PCCMDCHAN_IN_MAX)) {
            close("input backlog exceeded the ceiling");
            return;
        }
        in_.resize(old_size + RV_PCCMDCHAN_READ_CHUNK);
        const ssize_t got = ::read(STDIN_FILENO, in_.data() + old_size, RV_PCCMDCHAN_READ_CHUNK);
        if (got > 0) {
            in_.resize(old_size + static_cast<std::size_t>(got));
            taken += static_cast<std::size_t>(got);
            payload_progress_ = std::chrono::steady_clock::now();
            continue;
        }
        in_.resize(old_size);
        if (got == 0) {
            // EOF is NOT a disconnect yet. Everything already read is still a
            // request this console owes an answer to, and a command stream
            // redirected from a file reaches EOF on the first service call -
            // closing here would discard the entire session unread. The channel
            // goes down in next_request(), once the buffer is actually empty.
            eof_ = true;
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        close("read on stdin failed");
        return;
    }
}

void rv_pccmdchan_stdio::pump_out()
{
    while (connected_ && !out_.empty()) {
        const ssize_t put = ::write(STDOUT_FILENO, out_.data(), out_.size());
        if (put > 0) {
            out_.erase(0, static_cast<std::size_t>(put));
            continue;
        }
        if (put < 0 && errno == EINTR) {
            continue;
        }
        if (put < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return; // a slow reader is not an error; the queue waits its turn
        }
        close("write on stdout failed (the client stopped reading)");
        return;
    }
}

void rv_pccmdchan_stdio::reply(std::string_view line)
{
    if (!connected_) {
        return;
    }
    if (out_.size() + line.size() + 1 > static_cast<std::size_t>(RV_PCCMDCHAN_OUT_MAX)) {
        close("answer queue overflowed (the client is not reading)");
        return;
    }
    out_.append(line);
    out_.push_back('\n');
    pump_out();
}

void rv_pccmdchan_stdio::drain(std::chrono::milliseconds budget)
{
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (connected_ && !out_.empty() && std::chrono::steady_clock::now() < deadline) {
        pump_out();
        if (!out_.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool rv_pccmdchan_stdio::take_header(rv_pccmdreq &out)
{
    const char *base = in_.data();
    const std::size_t end = in_.size();
    std::size_t from = std::max(scanned_, consumed_);
    const char *found = std::find(base + from, base + end, '\n');
    if (found == base + end) {
        scanned_ = end;
        if (available() > static_cast<std::size_t>(RV_PCCMDCHAN_HEADER_MAX)) {
            close("request line exceeded the header ceiling");
        }
        return false;
    }

    const std::size_t line_begin = consumed_;
    std::size_t line_end = static_cast<std::size_t>(found - base);
    consumed_ = line_end + 1;
    scanned_ = consumed_;
    if (line_end > line_begin && in_[line_end - 1] == '\r') {
        --line_end; // an editor that sends CRLF is not making a protocol error
    }
    if (line_end - line_begin > static_cast<std::size_t>(RV_PCCMDCHAN_HEADER_MAX)) {
        // consumed_/scanned_ are already past this line, so it is dropped, not
        // rescanned forever, even though it is refused rather than parsed.
        close("request line exceeded the header ceiling");
        return false;
    }
    std::string_view line(base + line_begin, line_end - line_begin);

    rv_pccmdreq req;
    req.args = split_tokens(line);
    if (req.args.empty()) {
        return false; // a blank line is nothing at all, not an error
    }
    // The id is judged but NOT acted on yet: a refused header may still claim a
    // payload, and those bytes have to be framed away before anything is
    // answered. Refusing first left them in the stream to be read as commands -
    // `0 reload entry bytes 8` followed by eight bytes of "99 quit" shut the
    // console down.
    std::string id_error;
    if (!parse_u63(req.args.front(), req.id)) {
        id_error = "0 err error=protocol effects=0 msg=" +
            rv_pccmd_hex("first token must be a numeric request id");
    } else if (req.id == 0) {
        // 0 is reserved for unsolicited events (see rv_pconsole_run.cpp), so a
        // reply tagged 0 would be indistinguishable from one of those.
        id_error = "0 err error=protocol effects=0 msg=" +
            rv_pccmd_hex("request id must be greater than zero; zero is reserved for unsolicited events");
    }
    req.args.erase(req.args.begin());

    // A trailing `bytes <n>` is the channel's business, not the verb's: only the
    // channel can know where the next header starts, so it strips the pair here
    // and gathers the bytes itself.
    if (req.args.size() >= 2 && req.args[req.args.size() - 2] == "bytes") {
        int64_t size = 0;
        if (!parse_u63(req.args.back(), size) || size > RV_PCCMDCHAN_PAYLOAD_MAX) {
            // Fatal to the framing: the sender is about to write a number of
            // bytes we do not know, and guessing would turn them into commands.
            reply(std::to_string(req.id) + " err error=payload_size effects=0 msg=" +
                rv_pccmd_hex("payload size is not a number within the ceiling"));
            close("payload size could not be framed");
            return false;
        }
        req.args.resize(req.args.size() - 2);
        req.has_payload = true;
        need_ = static_cast<std::size_t>(size);
        pending_ = std::move(req);
        pending_error_ = std::move(id_error); // empty unless the id was refused
        phase_ = phase::payload;
        payload_started_ = std::chrono::steady_clock::now();
        payload_progress_ = payload_started_;
        return take_payload(out);
    }

    if (!id_error.empty()) {
        // No payload claimed, so the framing was never in doubt: answer and
        // carry on.
        reply(id_error);
        return false;
    }

    out = std::move(req);
    return true;
}

bool rv_pccmdchan_stdio::take_payload(rv_pccmdreq &out)
{
    if (available() >= need_) {
        pending_.payload.assign(cursor(), cursor() + need_);
        consumed_ += need_;
        scanned_ = consumed_;
        phase_ = phase::header;
        need_ = 0;
        if (!pending_error_.empty()) {
            // The bytes are eaten, so the next header starts where it should;
            // only now is the refused header answerable.
            reply(pending_error_);
            pending_error_.clear();
            pending_ = rv_pccmdreq{};
            return false;
        }
        out = std::move(pending_);
        pending_ = rv_pccmdreq{};
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - payload_progress_ > RV_PCCMDCHAN_PAYLOAD_IDLE ||
        now - payload_started_ > RV_PCCMDCHAN_PAYLOAD_TOTAL) {
        // Nothing can be salvaged: the bytes still to come have no marker, so
        // reading on would feed a half script's tail to the command parser.
        reply(std::to_string(pending_.id) + " err error=payload_timeout effects=0 msg=" +
            rv_pccmd_hex("payload did not arrive in time"));
        pending_error_.clear(); // the channel is going down; the id refusal is moot
        close("payload transfer timed out");
    }
    return false;
}

bool rv_pccmdchan_stdio::next_request(rv_pccmdreq &out)
{
    if (!connected_) {
        return false;
    }
    pump_out();
    pump_in();
    if (!connected_) {
        return false;
    }

    // A blank line and an unparseable id both consume a line without producing a
    // request. Stopping there would delay every request queued behind them by a
    // frame each, so the loop keeps going as long as it is making progress. The
    // guard is there because "progress" must never be allowed to mean "forever".
    bool got = false;
    for (int guard = 0; guard < 64 && connected_; ++guard) {
        const std::size_t before = consumed_;
        got = phase_ == phase::payload ? take_payload(out) : take_header(out);
        if (got || consumed_ == before) {
            break;
        }
    }
    compact();

    // Now, and only now, is the end of input final: everything that arrived has
    // been framed and handed over.
    if (!got && eof_ && connected_) {
        if (phase_ == phase::payload) {
            // A payload the sender will never finish. Nothing can be salvaged:
            // the missing bytes have no marker, so reading on would feed a half
            // script's tail to the command parser.
            reply(std::to_string(pending_.id) + " err error=payload_timeout effects=0 msg=" +
                rv_pccmd_hex("the client closed the channel mid-payload"));
            close("the client closed the channel mid-payload");
        } else if (available() == 0) {
            close("end of input (the client closed the channel)");
        }
    }
    return got;
}

std::unique_ptr<rv_pccmdchan> rv_pccmdchan_make()
{
    return std::make_unique<rv_pccmdchan_stdio>();
}

} // namespace rv_3dmppc
