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

int64_t rv_pccd_stream::play(const rv_pcmedium &medium, rv_pcca &ca, const char *resname, const rv_cd_stream_conf *conf)
{
    if (resname == nullptr || conf == nullptr) {
        RV_LOG_WARN("pccd", "stream play with a null name or conf");
        return RV_ERR_INVAL;
    }
    if (!rv_pcresname_valid(resname)) {
        RV_LOG_WARN("pccd", "stream play: '{}' is not a plain entry name", rv_pdklib::rv_log_escape(resname));
        return RV_ERR_INVAL;
    }
    if (conf->loop != 0 && conf->loop != 1) {
        RV_LOG_WARN("pccd", "stream play '{}': loop {} is not 0 or 1", rv_pdklib::rv_log_escape(resname), conf->loop);
        return RV_ERR_INVAL;
    }

    const int64_t size = medium.entry_size(resname);
    if (size < 0) {
        RV_LOG_WARN("pccd", "stream play '{}': entry unavailable (rv_err {})", rv_pdklib::rv_log_escape(resname), size);
        return size;
    }
    if (size == 0 || size % RV_PCCD_STREAM_BYTES_PER_FRAME != 0) {
        RV_LOG_WARN("pccd",
            "stream play '{}': size {} is not a whole number of S16 frames",
            rv_pdklib::rv_log_escape(resname),
            size);
        return RV_ERR_INVAL;
    }

    const int64_t frames = size / RV_PCCD_STREAM_BYTES_PER_FRAME;
    const int64_t end = conf->loop_end == 0 ? frames : conf->loop_end;
    if (conf->loop_start < 0 || conf->loop_start >= end || end > frames) {
        RV_LOG_WARN("pccd",
            "stream play '{}': loop [{}, {}) lies outside the {} frames of the entry",
            rv_pdklib::rv_log_escape(resname),
            conf->loop_start,
            end,
            frames);
        return RV_ERR_INVAL;
    }

    name_ = resname;
    frames_ = frames;
    loop_ = conf->loop;
    loop_start_ = conf->loop_start;
    loop_end_ = end;
    read_pos_ = 0;
    reader_active_ = true;
    state_ = RV_CD_STREAM_PLAYING;
    error_ = 0;
    ca.stream_clear();
    ca.stream_start();
    return RV_OK;
}

int64_t rv_pccd_stream::stop(rv_pcca &ca)
{
    ca.stream_clear();
    state_ = RV_CD_STREAM_IDLE;
    error_ = 0;
    reader_active_ = false;
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

} // namespace rv_3dmppc
