#include "rv_pconsole/cd/rv_pccd_stream.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

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
    // Passed stitches free their slots before the full check below.
    drop_passed_(ca->stream_report());
    // A full journal keeps the entry queued: pump takes it once a slot frees up.
    if (reader_active_ || journal_full_()) {
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
    journal_add_(entry, pushed_frames_, 0);
    load_(entry);
    has_pending_ = false;
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

void rv_pccd_stream::begin_entry_(const rv_stream_entry &entry, int64_t pos)
{
    load_(entry);
    read_pos_ = pos;
    state_ = RV_CD_STREAM_PLAYING;
    error_ = 0;
    last_starved_ = 0;
    starving_ = false;
    pushed_ = false;
    has_pending_ = false;
    pushed_frames_ = 0;
    journal_size_ = 0;
    journal_add_(entry, 0, pos);
}

void rv_pccd_stream::start_(rv_pcca &ca, const rv_stream_entry &entry)
{
    begin_entry_(entry, 0);
    starved_base_ = 0;
    switches_ = 0;
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
    // Emptied so queue_status cannot count passed stitches of a stopped stream; switches_ stays.
    journal_size_ = 0;
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
    rv_loop_position at{ 0, 0 };
    // The journal is read only while a stream runs; IDLE has no sounding entry.
    if (!idle) {
        drop_passed_(report);
        at = sound_position_(report);
    }

    out->state = state_;
    out->error = error_;
    out->position = at.position;
    out->loops = at.loops;
    out->starved_frames = idle ? 0 : starved_base_ + report.starved;
    return RV_OK;
}

int64_t rv_pccd_stream::switch_to(const rv_pcmedium &medium, rv_pcca *ca, const char *resname, const rv_cd_stream_conf *conf)
{
    rv_stream_entry entry;
    const int64_t err = measure_entry(medium, "switch", resname, conf, ca, entry);
    if (err != RV_OK) {
        return err;
    }
    const rv_pcstream_report report = ca->stream_report();
    drop_passed_(report);
    // A stream that just played out is ENDED now (settle_), so there is no position to keep: start as play.
    settle_(report);
    if (state_ != RV_CD_STREAM_PLAYING) {
        start_(*ca, entry);
        return RV_OK;
    }
    const rv_loop_position at = sound_position_(report);
    // Same bound as read_bound_() gives the new entry, read from its own start.
    const int64_t bound = entry.loop == 1 ? entry.loop_end : entry.frames;
    if (at.position >= bound) {
        RV_LOG_WARN("pccd",
            "stream switch to '{}' at position {}: not below the readable end {}",
            rv_pdklib::rv_log_escape(entry.name.c_str()),
            at.position,
            bound);
        return RV_ERR_INVAL;
    }

    // The restarted ring counts from zero: the old ring's starved frames move into the base.
    starved_base_ += report.starved;
    ca->stream_start();
    begin_entry_(entry, at.position);
    ++switches_;
    return RV_OK;
}

// Read position after `consumed` frames have been taken, for a stream that was played
// with these bounds. Loop 0 stops at the entry end; loop 1 wraps back to loop_start.
rv_pccd_stream::rv_loop_position
rv_pccd_stream::loop_position(int64_t loop, int64_t loop_start, int64_t end, int64_t frames, int64_t consumed)
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

rv_pccd_stream::rv_loop_position rv_pccd_stream::sound_position_(const rv_pcstream_report &report) const
{
    // Frames of the sounding entry played so far: its start position plus the consumer frames since its start.
    const rv_stream_entry &sound = journal_[0].entry;
    const int64_t played = journal_[0].start_pos + report.consumed - journal_[0].start_count;
    return loop_position(sound.loop, sound.loop_start, sound.loop_end, sound.frames, played);
}

int64_t rv_pccd_stream::queue_status(rv_pcca &ca, rv_cd_stream_queue_info *out)
{
    if (out == nullptr) {
        RV_LOG_WARN("pccd", "stream queue status with a null out");
        return RV_ERR_INVAL;
    }

    drop_passed_(ca.stream_report());
    out->pending = has_pending_ ? 1 : 0;
    out->switches = switches_;
    return RV_OK;
}

int64_t rv_pccd_stream::pump(const rv_pcmedium &medium, rv_pcca &ca)
{
    if (state_ != RV_CD_STREAM_PLAYING) {
        return RV_OK;
    }

    rv_pcstream_report report = ca.stream_report();
    warn_starved_(report);
    drop_passed_(report);
    // A queued entry waits for the closed input; the journal may have room now.
    if (!reader_active_ && has_pending_ && !journal_full_()) {
        const int64_t err = open_pending_(ca);
        if (err != RV_OK) {
            return err;
        }
        // start_ may have cleared the ring: the read and settle below need the report after it.
        report = ca.stream_report();
    }
    // A reader held at a boundary by a full journal takes the queued entry once a slot frees up.
    if (reader_active_) {
        wrap_or_end_(ca);
    }
    // Nothing to read: the reader is done, the ring is full, or it waits at a boundary. Still settle, so ENDED can appear.
    if (!reader_active_ || report.free == 0 || read_pos_ == read_bound_()) {
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
    pushed_frames_ += n;
    return RV_OK;
}

void rv_pccd_stream::wrap_or_end_(rv_pcca &ca)
{
    if (read_pos_ != read_bound_()) {
        return;
    }
    // The queued entry replaces both the loop wrap and the end; the ring input stays open.
    if (has_pending_) {
        // A full journal holds the reader here: no wrap, no end, until drop_passed_ frees a slot.
        if (journal_full_()) {
            return;
        }
        take_pending_();
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
    if (!reader_active_ && !has_pending_ && report.drained) {
        state_ = RV_CD_STREAM_ENDED;
    }
}

// A loop never reads past loop_end; a stream without a loop never reads past the entry.
int64_t rv_pccd_stream::read_bound_() const
{
    return loop_ == 1 ? loop_end_ : frames_;
}

void rv_pccd_stream::journal_add_(const rv_stream_entry &entry, int64_t start_count, int64_t start_pos)
{
    journal_[journal_size_] = { entry, start_count, start_pos };
    ++journal_size_;
}

bool rv_pccd_stream::journal_full_() const
{
    return journal_size_ == RV_PCCD_STREAM_JOURNAL_DEPTH;
}

// Keeps the last entry whose start the consumer reached: index 0 is the sounding one.
void rv_pccd_stream::drop_passed_(const rv_pcstream_report &report)
{
    while (journal_size_ > 1 && report.consumed >= journal_[1].start_count) {
        for (std::size_t i = 1; i < journal_size_; ++i) {
            journal_[i - 1] = std::move(journal_[i]);
        }
        --journal_size_;
        ++switches_;
    }
}

void rv_pccd_stream::take_pending_()
{
    journal_add_(pending_, pushed_frames_, 0);
    load_(pending_);
    has_pending_ = false;
}

int64_t rv_pccd_stream::open_pending_(rv_pcca &ca)
{
    const int64_t opened = ca.stream_reopen_input();
    if (opened == RV_ERR_INVAL) {
        start_(ca, pending_);
        return RV_OK;
    }
    if (opened != RV_OK) {
        return fail_(ca, opened);
    }
    take_pending_();
    return RV_OK;
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
