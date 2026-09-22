// The concrete development channel: stdin/stdout, non-blocking, framed as
// described in rv_pccmdchan.hpp. Only compiled into a -D3DMPPC_DEVTOOLS=ON
// build (see the dev-capability slot in CMakeLists.txt), so a player binary
// never carries these fields at all.
#pragma once

#include "rv_pconsole/platform/rv_pccmdchan.hpp"

namespace rv_3dmppc
{

class rv_pccmdchan_stdio final : public rv_pccmdchan
{
public:
    // Puts stdin and stdout into non-blocking mode and takes SIGPIPE off the
    // default disposition; the destructor puts all three back. Ignoring SIGPIPE
    // is what turns "the editor died mid-answer" into an EPIPE this class can
    // report, instead of a process that vanishes without a log line.
    rv_pccmdchan_stdio();
    ~rv_pccmdchan_stdio() override;

    bool next_request(rv_pccmdreq &out) override;
    void reply(std::string_view line) override;
    void drain(std::chrono::milliseconds budget) override;

    bool connected() const override
    {
        return connected_;
    }

    const char *closed_reason() const override
    {
        return reason_;
    }

private:
    enum class phase { header, payload };

    void pump_in();
    void pump_out();
    bool take_header(rv_pccmdreq &out);
    bool take_payload(rv_pccmdreq &out);
    void close(const char *why);
    void compact();
    std::size_t available() const
    {
        return in_.size() - consumed_;
    }
    const char *cursor() const
    {
        return in_.data() + consumed_;
    }

    std::vector<char> in_;
    std::size_t consumed_ = 0;   // bytes of in_ already framed away
    std::size_t scanned_ = 0;    // how far the newline search got last time
    std::string out_;

    phase phase_ = phase::header;
    rv_pccmdreq pending_;        // header parsed, payload still arriving
    std::size_t need_ = 0;
    // A header that claimed a payload AND was refused. The bytes still have to
    // be eaten - they carry no marker, so anything left in the stream would be
    // read as commands - and only once they are gone is the refusal answered.
    std::string pending_error_;
    std::chrono::steady_clock::time_point payload_started_;
    std::chrono::steady_clock::time_point payload_progress_;

    bool connected_ = true;
    const char *reason_ = "";

    // The far end stopped writing, but what it already wrote is still ours to
    // execute. Kept separate from connected_ for exactly that reason: a stream
    // redirected from a file hits EOF on the very first service call, and
    // treating that as a disconnect would throw away every command in it - the
    // whole session, unread. The channel closes only once the buffer is drained.
    bool eof_ = false;

    int in_flags_ = -1;
    int out_flags_ = -1;
    bool sigpipe_saved_ = false;
};

} // namespace rv_3dmppc
