// Streamed music on the drive: play, stop and status of one stream (rv_cd_stream_*).
// The drive owns the staging buffer that pump() fills; the ring belongs to the ca.
// play asks the medium only for the entry size; pump() reads the content.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdk/cd/rv_cd.h"

namespace rv_3dmppc
{

class rv_pcmedium;
class rv_pcca;

// Staging per pump: 8192 mono S16 frames, 16 KiB, about 186 ms at 44100. A device
// constant, not a manifest field.
constexpr int64_t RV_PCCD_STREAM_PORTION_FRAMES = 8192;

class rv_pccd_stream
{
public:
    rv_pccd_stream();

    // Every check runs before anything changes: a refusal leaves the current stream running.
    int64_t play(const rv_pcmedium &medium, rv_pcca &ca, const char *resname, const rv_cd_stream_conf *conf);

    // Idempotent, from any state.
    int64_t stop(rv_pcca &ca);

    int64_t status(rv_pcca &ca, rv_cd_stream_info *out);

private:
    std::string name_;
    int64_t frames_ = 0; // entry length in mono frames
    int64_t loop_ = 0;
    int64_t loop_start_ = 0;
    int64_t loop_end_ = 0; // effective: the entry end when the conf said 0
    int64_t read_pos_ = 0; // next entry frame to read
    bool reader_active_ = false;
    int64_t state_ = RV_CD_STREAM_IDLE;
    int64_t error_ = 0;
    std::vector<int16_t> staging_;
};

} // namespace rv_3dmppc
