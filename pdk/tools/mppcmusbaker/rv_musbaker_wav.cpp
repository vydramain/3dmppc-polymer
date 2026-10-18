// mppcmusbaker: the RIFF/WAVE chunk walk and the PCM downmix.
#include "rv_musbaker_wav.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>

#include "pdk/rv_err.h"

namespace
{

// Bit and byte constants
constexpr unsigned RV_BITS_PER_BYTE = 8; // bits per byte in little-endian assembly
constexpr uint8_t RV_BYTE_MASK = 0xFF;   // mask for extracting one byte

// RIFF/WAVE chunk structure: derived from tag and size field
constexpr size_t RV_TAG_LENGTH = 4;                                          // length of RIFF/WAVE/fmt/data ID strings
constexpr size_t RV_SIZE_FIELD_BYTES = 4;                                    // size of u32 chunk size field
constexpr size_t RV_CHUNK_HEADER_SIZE = RV_TAG_LENGTH + RV_SIZE_FIELD_BYTES; // tag + size
constexpr size_t RV_RIFF_HEADER_SIZE = RV_CHUNK_HEADER_SIZE + RV_TAG_LENGTH; // "RIFF" + size + "WAVE"

// fmt chunk offsets to WAVE PCM format fields
constexpr size_t RV_FMT_MIN_CHUNK_SIZE = 16;      // minimum fmt subchunk size
constexpr size_t RV_FMT_CHANNELS_OFF = 2;         // offset to NumChannels field (uint16)
constexpr size_t RV_FMT_SAMPLE_RATE_OFF = 4;      // offset to SampleRate field (uint32)
constexpr size_t RV_FMT_BITS_PER_SAMPLE_OFF = 14; // offset to BitsPerSample field (uint16)

// Output is the console's S16LE mono at 44100 Hz; input may be 16-bit or 24-bit PCM.
constexpr uint16_t RV_WAV_CHANNELS_STEREO = 2; // stereo channel count
constexpr size_t RV_OUT_BYTES_PER_SAMPLE = 2;  // bytes per output S16LE sample
constexpr int32_t RV_S16_MIN = std::numeric_limits<int16_t>::min();
constexpr int32_t RV_S16_MAX = std::numeric_limits<int16_t>::max();

// 24-bit to 16-bit: drop the low 8 bits, rounding half up (toward +inf) before the drop.
constexpr unsigned RV_DROPPED_BITS_24 = 8;
constexpr int32_t RV_HALF_LSB_24 = 1 << (RV_DROPPED_BITS_24 - 1);
constexpr uint32_t RV_SIGN_BIT_24 = 1u << 23;
constexpr int32_t RV_RANGE_24 = 1 << 24;

// Chunk alignment
constexpr size_t RV_CHUNK_ALIGN_BOUNDARY = 2; // chunks pad to even boundary

// --- little-endian field readers ---
//
// Spelled out byte by byte so the result does not depend on the host's
// endianness, same as pdklib/rv_zip/rv_zip_format.hpp does for its own format.

uint16_t read_le16(const uint8_t *p)
{
    return static_cast<uint16_t>(static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << RV_BITS_PER_BYTE));
}

uint32_t read_le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << RV_BITS_PER_BYTE) |
        (static_cast<uint32_t>(p[2]) << (2 * RV_BITS_PER_BYTE)) | (static_cast<uint32_t>(p[3]) << (3 * RV_BITS_PER_BYTE));
}

// The fmt chunk fields this tool cares about; everything else in the chunk
// (extensible masks, extra bytes) is not addressed to us and is skipped.
struct wav_fmt {
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
};

constexpr uint16_t RV_WAV_FORMAT_PCM = 1;
constexpr uint32_t RV_WAV_REQUIRED_RATE = 44100;
constexpr uint16_t RV_WAV_BITS_16 = 16;
constexpr uint16_t RV_WAV_BITS_24 = 24;
constexpr uint16_t RV_WAV_ACCEPTED_BITS[] = { RV_WAV_BITS_16, RV_WAV_BITS_24 };

// Reads the whole file into memory. WAV assets are small; a chunk walk over a
// single buffer is simpler and safer than seeking a FILE* back and forth. Returns
// RV_OK on success, RV_ERR_IO if the file could not be opened or read.
int read_whole_file(const std::string &path, std::vector<uint8_t> *out)
{
    std::FILE *in = std::fopen(path.c_str(), "rb");
    if (in == nullptr) {
        return RV_ERR_IO;
    }
    std::fseek(in, 0, SEEK_END);
    const long size = std::ftell(in);
    std::rewind(in);
    if (size < 0) {
        std::fclose(in);
        return RV_ERR_IO;
    }
    out->resize(static_cast<size_t>(size));
    const size_t read = out->empty() ? 0 : std::fread(out->data(), 1, out->size(), in);
    const bool ok = std::fclose(in) == 0 && read == out->size();
    return ok ? RV_OK : RV_ERR_IO;
}

bool is_accepted_bits(uint16_t bits)
{
    return std::find(std::begin(RV_WAV_ACCEPTED_BITS), std::end(RV_WAV_ACCEPTED_BITS), bits) != std::end(RV_WAV_ACCEPTED_BITS);
}

// Signed 24-bit little-endian sample, sign-extended to 32 bits.
int32_t read_le24(const uint8_t *p)
{
    const uint32_t raw = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << RV_BITS_PER_BYTE) |
        (static_cast<uint32_t>(p[2]) << (2 * RV_BITS_PER_BYTE));
    return (raw & RV_SIGN_BIT_24) != 0 ? static_cast<int32_t>(raw) - RV_RANGE_24 : static_cast<int32_t>(raw);
}

// Converts one sample of `bits` depth to 16-bit. Ties round up (toward +inf) for
// negative samples too, since the shift is an arithmetic floor. Saturation only
// matters at the top: 0x7FFFFF rounds to 32768 and is clamped to 32767.
int16_t sample_to_s16(const uint8_t *p, uint16_t bits)
{
    if (bits == RV_WAV_BITS_16) {
        return static_cast<int16_t>(read_le16(p));
    }
    const int32_t rounded = (read_le24(p) + RV_HALF_LSB_24) >> RV_DROPPED_BITS_24;
    return static_cast<int16_t>(std::clamp(rounded, RV_S16_MIN, RV_S16_MAX));
}

// Downmixes one stereo pair to mono, rounding toward zero: plain integer
// division already does that, so the sum is never adjusted for sign.
int16_t downmix_pair(int16_t left, int16_t right)
{
    return static_cast<int16_t>((static_cast<int32_t>(left) + static_cast<int32_t>(right)) / RV_WAV_CHANNELS_STEREO);
}

} // namespace

rv_err load_wav_pcm(const std::string &input, std::vector<uint8_t> *out, std::string *error)
{
    std::vector<uint8_t> file;
    if (read_whole_file(input, &file) != RV_OK) {
        *error = "cannot read '" + input + "'";
        return RV_ERR_IO;
    }
    if (file.size() < RV_RIFF_HEADER_SIZE || std::memcmp(file.data(), "RIFF", RV_TAG_LENGTH) != 0 ||
        std::memcmp(file.data() + RV_CHUNK_HEADER_SIZE, "WAVE", RV_TAG_LENGTH) != 0) {
        *error = "'" + input + "' is not a RIFF/WAVE file";
        return RV_ERR_INVAL;
    }

    wav_fmt fmt;
    bool have_fmt = false;
    const uint8_t *data = nullptr;
    size_t data_size = 0;

    size_t pos = RV_RIFF_HEADER_SIZE;
    while (pos + RV_CHUNK_HEADER_SIZE <= file.size()) {
        const uint8_t *chunk_id = file.data() + pos;
        const uint32_t chunk_size = read_le32(file.data() + pos + RV_TAG_LENGTH);
        pos += RV_CHUNK_HEADER_SIZE;
        if (chunk_size > file.size() - pos) {
            *error = "'" + input + "' has a truncated chunk";
            return RV_ERR_INVAL;
        }
        if (std::memcmp(chunk_id, "fmt ", RV_TAG_LENGTH) == 0 && chunk_size >= RV_FMT_MIN_CHUNK_SIZE) {
            const uint8_t *p = file.data() + pos;
            fmt.audio_format = read_le16(p);
            fmt.channels = read_le16(p + RV_FMT_CHANNELS_OFF);
            fmt.sample_rate = read_le32(p + RV_FMT_SAMPLE_RATE_OFF);
            fmt.bits_per_sample = read_le16(p + RV_FMT_BITS_PER_SAMPLE_OFF);
            have_fmt = true;
        } else if (std::memcmp(chunk_id, "data", RV_TAG_LENGTH) == 0) {
            data = file.data() + pos;
            data_size = chunk_size;
        }
        // Anything else (LIST/INFO and the like) is not addressed to us.
        pos += chunk_size;
        if (chunk_size % RV_CHUNK_ALIGN_BOUNDARY == 1) {
            pos += 1; // chunks pad to an even boundary
        }
    }

    if (!have_fmt) {
        *error = "'" + input + "' has no fmt chunk";
        return RV_ERR_INVAL;
    }
    if (data == nullptr) {
        *error = "'" + input + "' has no data chunk";
        return RV_ERR_INVAL;
    }
    // WAVE_FORMAT_EXTENSIBLE (0xFFFE) fails this check: its subformat GUID is not parsed.
    if (fmt.audio_format != RV_WAV_FORMAT_PCM) {
        *error = "'" + input + "' is WAVE format " + std::to_string(fmt.audio_format) + ", requires PCM (" +
            std::to_string(RV_WAV_FORMAT_PCM) + ")";
        return RV_ERR_INVAL;
    }
    if (!is_accepted_bits(fmt.bits_per_sample)) {
        *error = "'" + input + "' is " + std::to_string(fmt.bits_per_sample) + "-bit, requires 16-bit or 24-bit PCM";
        return RV_ERR_INVAL;
    }
    if (fmt.sample_rate != RV_WAV_REQUIRED_RATE) {
        *error = "'" + input + "' is " + std::to_string(fmt.sample_rate) + " Hz, requires " +
            std::to_string(RV_WAV_REQUIRED_RATE) + " Hz";
        return RV_ERR_INVAL;
    }
    if (fmt.channels != 1 && fmt.channels != RV_WAV_CHANNELS_STEREO) {
        *error = "'" + input + "' has " + std::to_string(fmt.channels) + " channels, requires mono or stereo";
        return RV_ERR_INVAL;
    }

    const size_t bytes_per_sample = fmt.bits_per_sample / RV_BITS_PER_BYTE;
    const size_t frame_size = fmt.channels * bytes_per_sample;
    if (data_size % frame_size != 0) {
        *error = "'" + input + "' data chunk is not a whole number of sample frames";
        return RV_ERR_INVAL;
    }
    const size_t frame_count = data_size / frame_size;
    out->resize(frame_count * RV_OUT_BYTES_PER_SAMPLE);
    for (size_t i = 0; i < frame_count; ++i) {
        const uint8_t *frame = data + i * frame_size;
        const int16_t left = sample_to_s16(frame, fmt.bits_per_sample);
        const int16_t mono =
            fmt.channels == 1 ? left : downmix_pair(left, sample_to_s16(frame + bytes_per_sample, fmt.bits_per_sample));
        (*out)[i * RV_OUT_BYTES_PER_SAMPLE] = static_cast<uint8_t>(mono & RV_BYTE_MASK);
        (*out)[i * RV_OUT_BYTES_PER_SAMPLE + 1] = static_cast<uint8_t>((mono >> RV_BITS_PER_BYTE) & RV_BYTE_MASK);
    }
    return RV_OK;
}
