// mppcmusbaker: the MP3 decode, frame by frame, into the console's S16LE mono.
#include "rv_musbaker_mp3.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// minimp3 is one header; its code exists in this translation unit only.
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "pdk/rv_err.h"

namespace
{

// The console plays one rate only. A file at any other rate is refused.
constexpr int RV_MP3_REQUIRED_RATE = 44100;

// Bit and byte constants, the same little-endian layout the WAV path writes.
constexpr unsigned RV_BITS_PER_BYTE = 8;
constexpr uint8_t RV_BYTE_MASK = 0xFF;
constexpr size_t RV_BYTES_PER_SAMPLE = 2;

// What the decoder produced: interleaved samples and the format of the first frame.
struct mp3_pcm {
    std::vector<int16_t> samples;
    int rate = 0;
    int channels = 0;
};

// ID3v2 leads a file, its header is 10 bytes and the size is syncsafe.
constexpr size_t RV_ID3V2_HEADER_SIZE = 10;
constexpr size_t RV_ID3V2_FLAGS_OFFSET = 5;
constexpr size_t RV_ID3V2_SIZE_OFFSET = 6;
constexpr uint8_t RV_ID3V2_FOOTER_FLAG = 0x10;
constexpr size_t RV_ID3V2_FOOTER_SIZE = 10;
constexpr unsigned RV_SYNCSAFE_BITS = 7;
constexpr uint8_t RV_SYNCSAFE_MASK = 0x7F;

// ID3v1 ends a file: 128 bytes starting with "TAG". It is not audio.
constexpr size_t RV_ID3V1_SIZE = 128;

// Only Layer III is the format the disc is written for; I and II decode too, but are refused.
constexpr int RV_MP3_REQUIRED_LAYER = 3;

// Bytes of a leading ID3v2 tag, header and footer included; 0 when there is none.
size_t id3v2_size(const std::vector<uint8_t> &file)
{
    if (file.size() < RV_ID3V2_HEADER_SIZE || std::string_view(reinterpret_cast<const char *>(file.data()), 3) != "ID3") {
        return 0;
    }
    size_t body = 0;
    for (size_t i = 0; i < 4; ++i) {
        body = (body << RV_SYNCSAFE_BITS) | (file[RV_ID3V2_SIZE_OFFSET + i] & RV_SYNCSAFE_MASK);
    }
    const size_t footer = (file[RV_ID3V2_FLAGS_OFFSET] & RV_ID3V2_FOOTER_FLAG) ? RV_ID3V2_FOOTER_SIZE : 0;
    return RV_ID3V2_HEADER_SIZE + body + footer;
}

// APEv2 tag: a 32-byte footer "APETAGEX" ends it, before any ID3v1. Its size field
// (little-endian, offset 12) counts the items and the footer; a header of the same
// size precedes them when bit 31 of the flags (offset 20) is set.
constexpr size_t RV_APE_FOOTER_SIZE = 32;
constexpr size_t RV_APE_SIZE_OFFSET = 12;
constexpr size_t RV_APE_FLAGS_OFFSET = 20;
constexpr uint32_t RV_APE_HEADER_FLAG = 0x80000000;

uint32_t read_le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}

// Bytes of an APEv2 tag that ends at `end`, header included; 0 when there is none or
// its size is not sane (the leftover bytes then refuse the file).
size_t ape_tag_size(const std::vector<uint8_t> &file, size_t end)
{
    if (end < RV_APE_FOOTER_SIZE ||
        std::string_view(reinterpret_cast<const char *>(file.data() + end - RV_APE_FOOTER_SIZE), 8) != "APETAGEX") {
        return 0;
    }
    const uint8_t *footer = file.data() + end - RV_APE_FOOTER_SIZE;
    const size_t items_and_footer = read_le32(footer + RV_APE_SIZE_OFFSET);
    const bool has_header = (read_le32(footer + RV_APE_FLAGS_OFFSET) & RV_APE_HEADER_FLAG) != 0;
    const size_t total = items_and_footer + (has_header ? RV_APE_FOOTER_SIZE : 0);
    return (items_and_footer < RV_APE_FOOTER_SIZE || total > end) ? 0 : total;
}

// End of the audio bytes: a trailing ID3v1 tag and an APEv2 tag before it are not audio.
size_t audio_end(const std::vector<uint8_t> &file)
{
    size_t end = file.size();
    if (end >= RV_ID3V1_SIZE &&
        std::string_view(reinterpret_cast<const char *>(file.data() + end - RV_ID3V1_SIZE), 3) == "TAG") {
        end -= RV_ID3V1_SIZE;
    }
    return end - ape_tag_size(file, end);
}

// Decodes every frame of the audio bytes. A frame that yields no samples, or bytes left
// undecoded at the end, is damage: the file is refused, not played with a hole in it.
// Returns RV_ERR_INVAL when no frame decodes, on damage, on another layer, or when the
// format changes mid-stream.
int decode_frames(const std::string &input, const std::vector<uint8_t> &file, mp3_pcm *out, std::string *error)
{
    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    int16_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];

    const size_t end = audio_end(file);
    const size_t tag = id3v2_size(file);
    size_t offset = 0;
    size_t decoded = 0;
    while (offset < end) {
        const int samples_per_channel =
            mp3dec_decode_frame(&dec, file.data() + offset, static_cast<int>(end - offset), frame, &info);
        if (info.frame_bytes == 0) {
            break;
        }
        if (samples_per_channel == 0) {
            // minimp3 consumes a leading ID3v2 tag without audio; anything else is damage.
            if (offset != 0 || static_cast<size_t>(info.frame_bytes) != tag) {
                *error = "'" + input + "' has damaged MPEG audio at byte " + std::to_string(offset);
                return RV_ERR_INVAL;
            }
            offset += static_cast<size_t>(info.frame_bytes);
            continue;
        }
        if (info.layer != RV_MP3_REQUIRED_LAYER) {
            *error = "'" + input + "' is MPEG Layer " + std::to_string(info.layer) + ", requires Layer III";
            return RV_ERR_INVAL;
        }
        offset += static_cast<size_t>(info.frame_bytes);
        if (decoded == 0) {
            out->rate = info.hz;
            out->channels = info.channels;
        } else if (info.hz != out->rate || info.channels != out->channels) {
            *error = "'" + input + "' changes rate or channel count in the middle of the stream";
            return RV_ERR_INVAL;
        }
        ++decoded;
        const size_t count = static_cast<size_t>(samples_per_channel) * static_cast<size_t>(info.channels);
        out->samples.insert(out->samples.end(), frame, frame + count);
    }

    if (decoded == 0) {
        *error = "'" + input + "' has no MPEG audio frames (not an MP3, or damaged)";
        return RV_ERR_INVAL;
    }
    if (offset != end) {
        *error = "'" + input + "' has " + std::to_string(end - offset) + " undecoded bytes at byte " + std::to_string(offset) +
            " (a truncated or damaged frame)";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

} // namespace

rv_err load_mp3_pcm(const std::string &input, std::vector<uint8_t> *out, std::string *error)
{
    std::ifstream in(input, std::ios::binary);
    if (!in) {
        *error = "cannot read '" + input + "'";
        return RV_ERR_IO;
    }
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        *error = "cannot read '" + input + "'";
        return RV_ERR_IO;
    }

    mp3_pcm pcm;
    if (decode_frames(input, file, &pcm, error) != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (pcm.rate != RV_MP3_REQUIRED_RATE) {
        *error =
            "'" + input + "' is " + std::to_string(pcm.rate) + " Hz, requires " + std::to_string(RV_MP3_REQUIRED_RATE) + " Hz";
        return RV_ERR_INVAL;
    }

    // Same downmix as the WAV path: average the channels with integer division,
    // which rounds toward zero. For mono the average is the sample itself.
    const size_t channels = static_cast<size_t>(pcm.channels);
    const size_t frame_count = pcm.samples.size() / channels;
    out->resize(frame_count * RV_BYTES_PER_SAMPLE);
    for (size_t i = 0; i < frame_count; ++i) {
        int32_t sum = 0;
        for (size_t c = 0; c < channels; ++c) {
            sum += pcm.samples[i * channels + c];
        }
        const int16_t mono = static_cast<int16_t>(sum / pcm.channels);
        (*out)[i * RV_BYTES_PER_SAMPLE] = static_cast<uint8_t>(mono & RV_BYTE_MASK);
        (*out)[i * RV_BYTES_PER_SAMPLE + 1] = static_cast<uint8_t>((mono >> RV_BITS_PER_BYTE) & RV_BYTE_MASK);
    }
    return RV_OK;
}
