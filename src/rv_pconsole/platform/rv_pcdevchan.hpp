// The development channel: the console's SECOND way in, and the only one
// besides the power switch.
//
// The frame loop has exactly one external input today - rv_pcplatform::
// quit_requested(). This adds one more, and only under --dev: a line protocol
// on stdin, answers on stdout. A player's console never constructs this class,
// so a shipped binary reads no commands at all - there is no "dev build" and no
// second frame loop, which is precisely what keeps the two from drifting apart.
//
// Process state, not a slot: rv_pcsignals is the shape this follows. A slot
// (rv_pcslots.hpp) is a controller the CONTRACT names, and pdk/ knows nothing
// about a channel - the disc must not learn that an editor exists.
//
// EVERYTHING HERE IS NON-BLOCKING. The channel is serviced once per frame from
// inside the frame loop, so a read that waits is a frame that never renders and
// a window that stops answering its own close button. The two places where the
// channel may still hold the loop are bounded and named: gathering an announced
// payload (bounded by the deadlines below) and the caller's own handling of a
// request.
//
// stdout carries the protocol and NOTHING else. Logs go to stderr already
// (rv_logs), and Lua's own print() is re-pointed at the logger by rv_pccl_luajit
// for this reason: a chunk that printed to stdout would splice its text into the
// answer stream, and the editor would parse it as a reply.
//
// A pure interface, on purpose: the concrete stdio channel (rv_pcdevchan_stdio)
// carries every buffer this protocol needs, and a player build never compiles
// that class at all. Splitting the interface away from its state is what keeps
// a player binary from holding seven fields nothing in it ever touches.
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rv_3dmppc
{

// Ceilings. Every one of them exists because the far end of this channel may be
// a program with a bug: a header that never ends, a payload size typed by hand,
// a reader that stopped reading. None of them may become an unbounded buffer.
constexpr int64_t RV_PCDEVCHAN_HEADER_MAX = 4096;          // one request line
constexpr int64_t RV_PCDEVCHAN_PAYLOAD_MAX = 4 << 20;      // one script
constexpr int64_t RV_PCDEVCHAN_OUT_MAX = 256 * 1024;       // answers not yet taken
constexpr int64_t RV_PCDEVCHAN_REQS_PER_TICK = 32;         // the CALLER honours this one
// Comfortably above HEADER_MAX + PAYLOAD_MAX so one legal request plus its
// payload never trips it, but still a bound: a sender that outruns the
// console must hit this instead of growing the backlog without limit.
constexpr int64_t RV_PCDEVCHAN_IN_MAX = RV_PCDEVCHAN_HEADER_MAX + RV_PCDEVCHAN_PAYLOAD_MAX + (1 << 20);

// Payload deadlines. `idle` is "no byte arrived for this long", `total` is "this
// transfer has gone on long enough" - a sender that dribbles one byte per second
// would defeat the first check alone.
constexpr auto RV_PCDEVCHAN_PAYLOAD_IDLE = std::chrono::seconds(5);
constexpr auto RV_PCDEVCHAN_PAYLOAD_TOTAL = std::chrono::seconds(30);

// One request, already framed. `args` holds the verb and its arguments with the
// trailing `bytes <n>` pair removed - the channel consumed that itself, because
// only the channel can know where the next header starts.
struct rv_pcdevreq {
    int64_t id = 0;
    std::vector<std::string> args;
    std::vector<char> payload;
    bool has_payload = false;

    // The verb, or "" for a request that carried nothing but an id.
    std::string_view verb() const
    {
        return args.empty() ? std::string_view() : std::string_view(args[0]);
    }
    std::string_view arg(std::size_t i) const
    {
        return i + 1 < args.size() ? std::string_view(args[i + 1]) : std::string_view();
    }
};

class rv_pcdevchan
{
public:
    virtual ~rv_pcdevchan() = default;

    rv_pcdevchan() = default;
    rv_pcdevchan(const rv_pcdevchan &) = delete;
    rv_pcdevchan &operator=(const rv_pcdevchan &) = delete;

    // Move input and output along, then hand back one framed request if one is
    // ready. False means "nothing complete right now" - never "wait".
    virtual bool next_request(rv_pcdevreq &out) = 0;

    // Queue one answer line (the newline is added here). Over the queue ceiling
    // the channel disconnects rather than grow: an answer nobody reads is not
    // worth a frame's memory.
    virtual void reply(std::string_view line) = 0;

    // Best effort, bounded by `budget`. Used on the way out so the last answer
    // reaches a client that is still there, without promising it did.
    virtual void drain(std::chrono::milliseconds budget) = 0;

    // False once the far end went away or broke the framing. A disconnected
    // channel answers nothing and reads nothing; the run carries on, and the
    // pause state is deliberately NOT touched - see rv_pconsole::dev_service.
    virtual bool connected() const = 0;

    // Why the channel went down, for the log line the caller writes once.
    virtual const char *closed_reason() const = 0;
};

// The one concrete channel, or nullptr where the player build excludes it
// (see the dev-capability slot in CMakeLists.txt).
std::unique_ptr<rv_pcdevchan> rv_pcdevchan_make();

} // namespace rv_3dmppc
