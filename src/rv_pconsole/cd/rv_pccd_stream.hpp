// Streamed music on the drive: play, stop, status and pump of one stream (rv_cd_stream_*).
// The drive owns the staging buffer that pump() fills; the ring belongs to the ca.
// play asks the medium only for the entry size; pump() is the only place that reads the content.
// A failed read becomes state ERROR, as the contract requires, and pump returns that code too;
// the console loop ignores it, since it can do nothing about it.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pdk/cd/rv_cd.h"

namespace rv_3dmppc
{

class rv_pcmedium;
class rv_pcca;
struct rv_pcstream_report;

// Staging per pump: 8192 mono S16 frames, 16 KiB, about 186 ms at 44100. A device
// constant, not a manifest field.
constexpr int64_t RV_PCCD_STREAM_PORTION_FRAMES = 8192;

// Stitch journal: the sounding entry and up to three more in the ring. A device constant, like the ring
// size; a full journal holds the reader at the boundary. Changed by measurement.
constexpr std::size_t RV_PCCD_STREAM_JOURNAL_DEPTH = 4;

class rv_pccd_stream
{
public:
    rv_pccd_stream();

    // Every check runs before anything changes: a refusal leaves the current stream running.
    // A null ca (no audio attached) is refused with RV_ERR_IO, after the argument, entry and loop checks.
    int64_t play(const rv_pcmedium &medium, rv_pcca *ca, const char *resname, const rv_cd_stream_conf *conf);

    // Queues the entry for the boundary of the current one; checks and codes as play, a refusal changes nothing.
    // Not PLAYING: starts it as play. A queue before the reader takes it replaces it.
    // The reader already closed the input: the input reopens and the entry starts from frame 0 at once,
    // unless the journal is full: then pump takes it later.
    int64_t queue(const rv_pcmedium &medium, rv_pcca *ca, const char *resname, const rv_cd_stream_conf *conf);

    // Idempotent, from any state.
    int64_t stop(rv_pcca &ca);

    // position and loops belong to the sounding entry (the stitch journal's first entry).
    int64_t status(rv_pcca &ca, rv_cd_stream_info *out);

    // pending is 1 while a queued entry waits; switches counts sounding-entry changes since play.
    int64_t queue_status(rv_pcca &ca, rv_cd_stream_queue_info *out);

    // Moves one portion from the medium into the ring when there is room and the reader is active.
    // RV_OK when there is nothing to do (not PLAYING, reader done, ring full); otherwise the rv_err
    // that became error_: INVAL from the medium maps to RV_ERR_IO, NOENT stays NOENT.
    int64_t pump(const rv_pcmedium &medium, rv_pcca &ca);

private:
    // One entry as measured by play or queue; loop_end is the effective end.
    struct rv_stream_entry {
        std::string name;
        int64_t frames = 0;
        int64_t loop = 0;
        int64_t loop_start = 0;
        int64_t loop_end = 0;
    };

    // A stitch: the entry whose first frame sounds when the consumer count reaches start_count.
    struct rv_junction {
        rv_stream_entry entry;
        int64_t start_count = 0; // frames pushed before this entry: the consumer count at its first frame
        int64_t start_pos = 0;   // position in the entry of its first frame
    };

    // Checks shared by play and queue, in play's order; `op` names the call in the log.
    static int64_t measure_entry(const rv_pcmedium &medium,
        const char *op,
        const char *resname,
        const rv_cd_stream_conf *conf,
        const rv_pcca *ca,
        rv_stream_entry &out);
    // Enters ERROR with `error`, clears the ring and stops the reader. Logs once. Returns `error`.
    int64_t fail_(rv_pcca &ca, int64_t error);
    // Makes `entry` the current one, read from frame 0.
    void load_(const rv_stream_entry &entry);
    // Starts `entry` as play does: counters reset, queue dropped, ring cleared and started.
    void start_(rv_pcca &ca, const rv_stream_entry &entry);
    // Logs a warning once per starvation episode, and only after the first push.
    void warn_starved_(const rv_pcstream_report &report);
    // Reads n frames at read_pos_ and pushes them; RV_OK, or the code fail_ returned.
    int64_t read_and_push_(const rv_pcmedium &medium, rv_pcca &ca, int64_t n);
    // After a portion: a queued entry takes over; else loop 1 wraps to loop_start_, and without a loop
    // the reader ends and the ring input closes.
    void wrap_or_end_(rv_pcca &ca);
    // ENDED once the reader is done and the ring has drained.
    void settle_(const rv_pcstream_report &report);
    // Appends a journal entry; callers check journal_full_() first.
    void journal_add_(const rv_stream_entry &entry, int64_t start_count, int64_t start_pos);
    bool journal_full_() const;
    // Drops the entries the consumer has passed; each one is a switch.
    void drop_passed_(const rv_pcstream_report &report);
    // Takes the queued entry into the journal and the reader, from frame 0.
    void take_pending_();
    // Reopens the ring input for the queued entry, then takes it; INVAL means the ring has played out: play it.
    int64_t open_pending_(rv_pcca &ca);
    int64_t read_bound_() const;

    std::string name_;
    int64_t frames_ = 0; // entry length in mono frames
    int64_t loop_ = 0;
    int64_t loop_start_ = 0;
    int64_t loop_end_ = 0; // effective: the entry end when the conf said 0
    int64_t read_pos_ = 0; // next entry frame to read
    bool reader_active_ = false;
    int64_t state_ = RV_CD_STREAM_IDLE;
    int64_t error_ = 0;
    int64_t last_starved_ = 0; // starved frames seen by the previous pump
    bool starving_ = false;    // the previous pump saw growth: its warning is out
    bool pushed_ = false;      // a portion has reached the ring since play
    rv_stream_entry pending_;  // queued entry, taken by the reader at the boundary
    bool has_pending_ = false;
    // Index 0 is the sounding entry; drop_passed_ keeps it so.
    std::array<rv_junction, RV_PCCD_STREAM_JOURNAL_DEPTH> journal_;
    std::size_t journal_size_ = 0;
    int64_t pushed_frames_ = 0; // frames pushed to the ring since start_
    int64_t switches_ = 0;      // sounding-entry changes since play
    std::vector<int16_t> staging_;
};

} // namespace rv_3dmppc
