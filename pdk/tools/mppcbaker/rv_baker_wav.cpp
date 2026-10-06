// mppcbaker: the RIFF/WAVE chunk walk and the PCM downmix.
#include "rv_baker_wav.hpp"

#include <cstdio>
#include <cstring>

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

// PCM audio parameters: 16-bit stereo at 44100 Hz
constexpr uint16_t RV_WAV_CHANNELS_STEREO = 2;                                        // stereo channel count
constexpr size_t RV_BYTES_PER_SAMPLE = 2;                                             // bytes per 16-bit sample
constexpr size_t RV_STEREO_FRAME_SIZE = RV_WAV_CHANNELS_STEREO * RV_BYTES_PER_SAMPLE; // stereo frame size

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
constexpr uint16_t RV_WAV_REQUIRED_BITS = 16;

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

// Downmixes one interleaved stereo frame to mono, rounding toward zero: plain
// integer division already does that, so the sum is never adjusted for sign.
int16_t downmix_frame(const uint8_t *frame)
{
    const int32_t left = static_cast<int16_t>(read_le16(frame));
    const int32_t right = static_cast<int16_t>(read_le16(frame + RV_BYTES_PER_SAMPLE));
    return static_cast<int16_t>((left + right) / RV_WAV_CHANNELS_STEREO);
}

} // namespace

rv_err load_wav_pcm(const std::string &input, std::vector<uint8_t> *out, baker_error *error)
{
    std::vector<uint8_t> file;
    if (read_whole_file(input, &file) != RV_OK) {
        error->message = "cannot read '" + input + "'";
        return RV_ERR_IO;
    }
    if (file.size() < RV_RIFF_HEADER_SIZE || std::memcmp(file.data(), "RIFF", RV_TAG_LENGTH) != 0 ||
        std::memcmp(file.data() + RV_CHUNK_HEADER_SIZE, "WAVE", RV_TAG_LENGTH) != 0) {
        error->message = "'" + input + "' is not a RIFF/WAVE file";
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
            error->message = "'" + input + "' has a truncated chunk";
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
        error->message = "'" + input + "' has no fmt chunk";
        return RV_ERR_INVAL;
    }
    if (data == nullptr) {
        error->message = "'" + input + "' has no data chunk";
        return RV_ERR_INVAL;
    }
    if (fmt.audio_format != RV_WAV_FORMAT_PCM) {
        error->message = "'" + input + "' is WAVE format " + std::to_string(fmt.audio_format) + ", mppcbaker requires PCM (1)";
        return RV_ERR_INVAL;
    }
    if (fmt.bits_per_sample != RV_WAV_REQUIRED_BITS) {
        error->message = "'" + input + "' is " + std::to_string(fmt.bits_per_sample) + "-bit, mppcbaker requires 16-bit PCM";
        return RV_ERR_INVAL;
    }
    if (fmt.sample_rate != RV_WAV_REQUIRED_RATE) {
        error->message = "'" + input + "' is " + std::to_string(fmt.sample_rate) + " Hz, mppcbaker requires 44100 Hz";
        return RV_ERR_INVAL;
    }
    if (fmt.channels != 1 && fmt.channels != RV_WAV_CHANNELS_STEREO) {
        error->message = "'" + input + "' has " + std::to_string(fmt.channels) + " channels, mppcbaker requires mono or stereo";
        return RV_ERR_INVAL;
    }

    if (fmt.channels == 1) {
        if (data_size % RV_BYTES_PER_SAMPLE != 0) {
            error->message = "'" + input + "' data chunk is not a whole number of samples";
            return RV_ERR_INVAL;
        }
        out->assign(data, data + data_size); // already S16LE mono, byte for byte
        return RV_OK;
    }

    if (data_size % RV_STEREO_FRAME_SIZE != 0) {
        error->message = "'" + input + "' data chunk is not a whole number of stereo frames";
        return RV_ERR_INVAL;
    }
    const size_t frame_count = data_size / RV_STEREO_FRAME_SIZE;
    out->resize(frame_count * RV_BYTES_PER_SAMPLE);
    for (size_t i = 0; i < frame_count; ++i) {
        const int16_t mono = downmix_frame(data + i * RV_STEREO_FRAME_SIZE);
        (*out)[i * RV_BYTES_PER_SAMPLE] = static_cast<uint8_t>(mono & RV_BYTE_MASK);
        (*out)[i * RV_BYTES_PER_SAMPLE + 1] = static_cast<uint8_t>((mono >> RV_BITS_PER_BYTE) & RV_BYTE_MASK);
    }
    return RV_OK;
}
