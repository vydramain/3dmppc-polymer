// Streamed music on the drive: play, stop, status and pump of one stream (rv_cd_stream_*).
// The drive owns the staging buffer that pump() fills; the ring belongs to the ca.
// play asks the medium only for the entry size; pump() is the only place that reads the content.
// A failed read becomes state ERROR, as the contract requires, and pump returns that code too;
// the console loop ignores it, since it can do nothing about it.
#pragma once

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

class rv_pccd_stream
{
public:
    rv_pccd_stream();

    // Every check runs before anything changes: a refusal leaves the current stream running.
    // A null ca (no audio attached) is refused with RV_ERR_IO, after the argument, entry and loop checks.
    int64_t play(const rv_pcmedium &medium, rv_pcca *ca, const char *resname, const rv_cd_stream_conf *conf);

    // Idempotent, from any state.
    int64_t stop(rv_pcca &ca);

    int64_t status(rv_pcca &ca, rv_cd_stream_info *out);

    // Moves one portion from the medium into the ring when there is room and the reader is active.
    // RV_OK when there is nothing to do (not PLAYING, reader done, ring full); otherwise the rv_err
    // that became error_: INVAL from the medium maps to RV_ERR_IO, NOENT stays NOENT.
    int64_t pump(const rv_pcmedium &medium, rv_pcca &ca);

private:
    // Enters ERROR with `error`, clears the ring and stops the reader. Logs once. Returns `error`.
    int64_t fail_(rv_pcca &ca, int64_t error);
    // Logs a warning once per starvation episode, and only after the first push.
    void warn_starved_(const rv_pcstream_report &report);
    // Reads n frames at read_pos_ and pushes them; RV_OK, or the code fail_ returned.
    int64_t read_and_push_(const rv_pcmedium &medium, rv_pcca &ca, int64_t n);
    // After a portion: loop 1 wraps to loop_start_; without a loop the reader ends and the ring input closes.
    void wrap_or_end_(rv_pcca &ca);
    // ENDED once the reader is done and the ring has drained.
    void settle_(const rv_pcstream_report &report);
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
    std::vector<int16_t> staging_;
};

} // namespace rv_3dmppc
