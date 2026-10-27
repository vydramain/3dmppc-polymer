#include "rv_pconsole/cd/rv_pccd_stream.hpp"

#include <algorithm>
#include <cstddef>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cd/rv_pcmedium.hpp"

namespace rv_3dmppc
{

namespace
{

// Entry sizes are bytes; one frame is one mono S16 sample.
constexpr int64_t RV_PCCD_STREAM_BYTES_PER_FRAME = 2;

struct rv_loop_position {
    int64_t position;
    int64_t loops;
};

// Read position after `consumed` frames have been taken, for a stream that was played
// with these bounds. Loop 0 stops at the entry end; loop 1 wraps back to loop_start.
rv_loop_position loop_position(int64_t loop, int64_t loop_start, int64_t end, int64_t frames, int64_t consumed)
{
    if (loop == 0) {
        return { std::min(consumed, frames), 0 };
    }
    if (consumed < end) {
        return { consumed, 0 };
    }
    const int64_t period = end - loop_start;
    const int64_t past_end = consumed - end;
    return { loop_start + past_end % period, 1 + past_end / period };
}

} // namespace

rv_pccd_stream::rv_pccd_stream()
    : staging_(static_cast<std::size_t>(RV_PCCD_STREAM_PORTION_FRAMES))
{
}

int64_t rv_pccd_stream::measure_entry(const rv_pcmedium &medium,
    const char *op,
    const char *resname,
    const rv_cd_stream_conf *conf,
    const rv_pcca *ca,
    rv_stream_entry &out)
{
    if (resname == nullptr || conf == nullptr) {
        RV_LOG_WARN("pccd", "stream {} with a null name or conf", op);
        return RV_ERR_INVAL;
    }
    if (!rv_pcresname_valid(resname)) {
        RV_LOG_WARN("pccd", "stream {}: '{}' is not a plain entry name", op, rv_pdklib::rv_log_escape(resname));
        return RV_ERR_INVAL;
    }
    if (conf->loop != 0 && conf->loop != 1) {
        RV_LOG_WARN("pccd", "stream {} '{}': loop {} is not 0 or 1", op, rv_pdklib::rv_log_escape(resname), conf->loop);
        return RV_ERR_INVAL;
    }

    const int64_t size = medium.entry_size(resname);
    if (size < 0) {
        RV_LOG_WARN("pccd", "stream {} '{}': entry unavailable (rv_err {})", op, rv_pdklib::rv_log_escape(resname), size);
        return size;
    }
    if (size == 0 || size % RV_PCCD_STREAM_BYTES_PER_FRAME != 0) {
        RV_LOG_WARN("pccd",
            "stream {} '{}': size {} is not a whole number of S16 frames",
            op,
            rv_pdklib::rv_log_escape(resname),
            size);
        return RV_ERR_INVAL;
    }

    const int64_t frames = size / RV_PCCD_STREAM_BYTES_PER_FRAME;
    const int64_t end = conf->loop_end == 0 ? frames : conf->loop_end;
    if (conf->loop_start < 0 || conf->loop_start >= end || end > frames) {
        RV_LOG_WARN("pccd",
            "stream {} '{}': loop [{}, {}) lies outside the {} frames of the entry",
            op,
            rv_pdklib::rv_log_escape(resname),
            conf->loop_start,
            end,
            frames);
        return RV_ERR_INVAL;
    }
    if (ca == nullptr) {
        RV_LOG_WARN("pccd", "stream {} '{}': no audio attached", op, rv_pdklib::rv_log_escape(resname));
        return RV_ERR_IO;
    }

    out.name = resname;
    out.frames = frames;
    out.loop = conf->loop;
    out.loop_start = conf->loop_start;
    out.loop_end = end;
    return RV_OK;
}

int64_t rv_pccd_stream::play(const rv_pcmedium &medium, rv_pcca *ca, const char *resname, const rv_cd_stream_conf *conf)
{
    rv_stream_entry entry;
    const int64_t err = measure_entry(medium, "play", resname, conf, ca, entry);
    if (err != RV_OK) {
        return err;
    }
    start_(*ca, entry);
    return RV_OK;
}

int64_t rv_pccd_stream::queue(const rv_pcmedium &medium, rv_pcca *ca, const char *resname, const rv_cd_stream_conf *conf)
{
    rv_stream_entry entry;
    const int64_t err = measure_entry(medium, "queue", resname, conf, ca, entry);
    if (err != RV_OK) {
        return err;
    }
    if (state_ != RV_CD_STREAM_PLAYING) {
        start_(*ca, entry);
        return RV_OK;
    }
    if (reader_active_) {
        pending_ = entry;
        has_pending_ = true;
        return RV_OK;
    }
    // The reader closed the input after its last entry; the ring still holds that entry's frames.
    const int64_t opened = ca->stream_reopen_input();
    // The ring has played out but ENDED is not set yet: nothing to continue, so start as play.
    if (opened == RV_ERR_INVAL) {
        start_(*ca, entry);
        return RV_OK;
    }
    if (opened != RV_OK) {
        RV_LOG_ERR("pccd",
            "stream queue '{}' behind '{}': reopening the ring input failed (rv_err {})",
            rv_pdklib::rv_log_escape(entry.name.c_str()),
            rv_pdklib::rv_log_escape(name_.c_str()),
            opened);
        return opened;
    }
    load_(entry);
    return RV_OK;
}

void rv_pccd_stream::load_(const rv_stream_entry &entry)
{
    name_ = entry.name;
    frames_ = entry.frames;
    loop_ = entry.loop;
    loop_start_ = entry.loop_start;
    loop_end_ = entry.loop_end;
    read_pos_ = 0;
    reader_active_ = true;
}

void rv_pccd_stream::start_(rv_pcca &ca, const rv_stream_entry &entry)
{
    load_(entry);
    state_ = RV_CD_STREAM_PLAYING;
    error_ = 0;
    last_starved_ = 0;
    starving_ = false;
    pushed_ = false;
    has_pending_ = false;
    ca.stream_clear();
    ca.stream_start();
}

int64_t rv_pccd_stream::stop(rv_pcca &ca)
{
    ca.stream_clear();
    state_ = RV_CD_STREAM_IDLE;
    error_ = 0;
    reader_active_ = false;
    has_pending_ = false;
    return RV_OK;
}

int64_t rv_pccd_stream::status(rv_pcca &ca, rv_cd_stream_info *out)
{
    if (out == nullptr) {
        RV_LOG_WARN("pccd", "stream status with a null out");
        return RV_ERR_INVAL;
    }

    const rv_pcstream_report report = ca.stream_report();
    const bool idle = state_ == RV_CD_STREAM_IDLE;
    const rv_loop_position at =
        idle ? rv_loop_position{ 0, 0 } : loop_position(loop_, loop_start_, loop_end_, frames_, report.consumed);

    out->state = state_;
    out->error = error_;
    out->position = at.position;
    out->loops = at.loops;
    out->starved_frames = idle ? 0 : report.starved;
    return RV_OK;
}

int64_t rv_pccd_stream::pump(const rv_pcmedium &medium, rv_pcca &ca)
{
    if (state_ != RV_CD_STREAM_PLAYING) {
        return RV_OK;
    }

    const rv_pcstream_report report = ca.stream_report();
    warn_starved_(report);
    // Nothing to read: the reader is done or the ring is full. Still settle, so ENDED can appear.
    if (!reader_active_ || report.free == 0) {
        settle_(report);
        return RV_OK;
    }

    // One read per pump, so a pump costs the same whatever the medium. Web pumps at rAF (~60 Hz):
    // a loop shorter than ~735 frames (44100 / 60) starves between pumps there. Native pumps every
    // 1 ms slice and has a wide margin. Starvation is counted by the mixer.
    const int64_t n = std::min({ report.free, RV_PCCD_STREAM_PORTION_FRAMES, read_bound_() - read_pos_ });
    const int64_t err = read_and_push_(medium, ca, n);
    if (err != RV_OK) {
        return err;
    }
    read_pos_ += n;
    wrap_or_end_(ca);
    settle_(report);
    return RV_OK;
}

void rv_pccd_stream::warn_starved_(const rv_pcstream_report &report)
{
    // Silence before the first push is the wait for the first portion, not a starved episode.
    const bool starving = pushed_ && report.starved > last_starved_;
    if (starving && !starving_) {
        RV_LOG_WARN("pccd",
            "stream '{}': ring empty, {} silent frames since the last pump",
            rv_pdklib::rv_log_escape(name_.c_str()),
            report.starved - last_starved_);
    }
    starving_ = starving;
    last_starved_ = report.starved;
}

int64_t rv_pccd_stream::read_and_push_(const rv_pcmedium &medium, rv_pcca &ca, int64_t n)
{
    const int64_t bytes = n * RV_PCCD_STREAM_BYTES_PER_FRAME;
    const int64_t got = medium.entry_read_at(name_.c_str(), read_pos_ * RV_PCCD_STREAM_BYTES_PER_FRAME, staging_.data(), bytes);
    // INVAL: the entry shrank after play measured it, so the content changed under us.
    if (got < 0) {
        return fail_(ca, got == RV_ERR_INVAL ? RV_ERR_IO : got);
    }
    if (got != bytes) {
        return fail_(ca, RV_ERR_IO);
    }
    // One producer; the consumer only drains, so the room from the report cannot shrink before the push.
    if (ca.stream_push(staging_.data(), n) != n) {
        return fail_(ca, RV_ERR_IO);
    }
    pushed_ = true;
    return RV_OK;
}

void rv_pccd_stream::wrap_or_end_(rv_pcca &ca)
{
    if (read_pos_ != read_bound_()) {
        return;
    }
    // The queued entry replaces both the loop wrap and the end; the ring input stays open.
    if (has_pending_) {
        load_(pending_);
        has_pending_ = false;
        return;
    }
    if (loop_ == 1) {
        read_pos_ = loop_start_;
        return;
    }
    reader_active_ = false;
    ca.stream_end_input();
}

void rv_pccd_stream::settle_(const rv_pcstream_report &report)
{
    // The ring may still hold frames here; ENDED waits for a pump that sees it drained.
    if (!reader_active_ && report.drained) {
        state_ = RV_CD_STREAM_ENDED;
    }
}

// A loop never reads past loop_end; a stream without a loop never reads past the entry.
int64_t rv_pccd_stream::read_bound_() const
{
    return loop_ == 1 ? loop_end_ : frames_;
}

int64_t rv_pccd_stream::fail_(rv_pcca &ca, int64_t error)
{
    RV_LOG_ERR("pccd", "stream '{}' stopped at frame {}: rv_err {}", rv_pdklib::rv_log_escape(name_.c_str()), read_pos_, error);
    state_ = RV_CD_STREAM_ERROR;
    error_ = error;
    reader_active_ = false;
    has_pending_ = false;
    ca.stream_clear();
    return error;
}

} // namespace rv_3dmppc
