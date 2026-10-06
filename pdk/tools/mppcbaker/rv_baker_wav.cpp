// mppcbaker: the RIFF/WAVE chunk walk and the PCM downmix.
#include "rv_baker_wav.hpp"

#include <cstdio>
#include <cstring>

#include "pdk/rv_err.h"

namespace {

// --- little-endian field readers ---
//
// Spelled out byte by byte so the result does not depend on the host's
// endianness, same as pdklib/rv_zip/rv_zip_format.hpp does for its own format.

uint16_t read_le16(const uint8_t *p)
{
    return static_cast<uint16_t>(static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8));
}

uint32_t read_le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
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
    const int32_t right = static_cast<int16_t>(read_le16(frame + 2));
    return static_cast<int16_t>((left + right) / 2);
}

} // namespace

rv_err load_wav_pcm(const std::string &input, std::vector<uint8_t> *out, baker_error *error)
{
    std::vector<uint8_t> file;
    if (read_whole_file(input, &file) != RV_OK) {
        error->message = "cannot read '" + input + "'";
        return RV_ERR_IO;
    }
    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0 || std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
        error->message = "'" + input + "' is not a RIFF/WAVE file";
        return RV_ERR_INVAL;
    }

    wav_fmt fmt;
    bool have_fmt = false;
    const uint8_t *data = nullptr;
    size_t data_size = 0;

    size_t pos = 12;
    while (pos + 8 <= file.size()) {
        const uint8_t *chunk_id = file.data() + pos;
        const uint32_t chunk_size = read_le32(file.data() + pos + 4);
        pos += 8;
        if (chunk_size > file.size() - pos) {
            error->message = "'" + input + "' has a truncated chunk";
            return RV_ERR_INVAL;
        }
        if (std::memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            const uint8_t *p = file.data() + pos;
            fmt.audio_format = read_le16(p);
            fmt.channels = read_le16(p + 2);
            fmt.sample_rate = read_le32(p + 4);
            fmt.bits_per_sample = read_le16(p + 14);
            have_fmt = true;
        } else if (std::memcmp(chunk_id, "data", 4) == 0) {
            data = file.data() + pos;
            data_size = chunk_size;
        }
        // Anything else (LIST/INFO and the like) is not addressed to us.
        pos += chunk_size;
        if (chunk_size % 2 == 1) {
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
        error->message = "'" + input + "' is WAVE format " + std::to_string(fmt.audio_format) +
            ", mppcbaker requires PCM (1)";
        return RV_ERR_INVAL;
    }
    if (fmt.bits_per_sample != RV_WAV_REQUIRED_BITS) {
        error->message =
            "'" + input + "' is " + std::to_string(fmt.bits_per_sample) + "-bit, mppcbaker requires 16-bit PCM";
        return RV_ERR_INVAL;
    }
    if (fmt.sample_rate != RV_WAV_REQUIRED_RATE) {
        error->message =
            "'" + input + "' is " + std::to_string(fmt.sample_rate) + " Hz, mppcbaker requires 44100 Hz";
        return RV_ERR_INVAL;
    }
    if (fmt.channels != 1 && fmt.channels != 2) {
        error->message =
            "'" + input + "' has " + std::to_string(fmt.channels) + " channels, mppcbaker requires mono or stereo";
        return RV_ERR_INVAL;
    }

    if (fmt.channels == 1) {
        if (data_size % 2 != 0) {
            error->message = "'" + input + "' data chunk is not a whole number of samples";
            return RV_ERR_INVAL;
        }
        out->assign(data, data + data_size); // already S16LE mono, byte for byte
        return RV_OK;
    }

    if (data_size % 4 != 0) {
        error->message = "'" + input + "' data chunk is not a whole number of stereo frames";
        return RV_ERR_INVAL;
    }
    const size_t frame_count = data_size / 4;
    out->resize(frame_count * 2);
    for (size_t i = 0; i < frame_count; ++i) {
        const int16_t mono = downmix_frame(data + i * 4);
        (*out)[i * 2] = static_cast<uint8_t>(mono & 0xFF);
        (*out)[i * 2 + 1] = static_cast<uint8_t>((mono >> 8) & 0xFF);
    }
    return RV_OK;
}
