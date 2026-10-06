// SHA-256 over a file, streamed in blocks, for a disc image's identity.

#include "release/rv_editor_sha256.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

namespace rv_editor
{

namespace
{

// Round constants: FIPS 180-4, section 4.2.2.
constexpr std::array<uint32_t, 64> rv_editor_sha256_k = { 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc,
    0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1,
    0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814,
    0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

// Rotation amounts for Sigma functions: FIPS 180-4, section 4.1.2.
constexpr int big_sigma0_rot_a = 2;
constexpr int big_sigma0_rot_b = 13;
constexpr int big_sigma0_rot_c = 22;
constexpr int big_sigma1_rot_a = 6;
constexpr int big_sigma1_rot_b = 11;
constexpr int big_sigma1_rot_c = 25;

// Rotation amounts for sigma functions: FIPS 180-4, section 4.1.2.
constexpr int sigma0_rot_a = 7;
constexpr int sigma0_rot_b = 18;
constexpr int sigma0_rot_c = 3;
constexpr int sigma1_rot_a = 17;
constexpr int sigma1_rot_b = 19;
constexpr int sigma1_rot_c = 10;

// Message schedule block size: FIPS 180-4, section 5.2.1.
constexpr int block_size = 64;

// Initial message schedule length before expansion: FIPS 180-4, section 6.2.2.
constexpr int message_schedule_initial = 16;

// Full message schedule length after expansion: FIPS 180-4, section 6.2.2.
constexpr int message_schedule_full = 64;

// Message schedule expansion lags: FIPS 180-4, section 6.2.2 step 1.
constexpr int w_schedule_lag_2 = 2;
constexpr int w_schedule_lag_7 = 7;
constexpr int w_schedule_lag_15 = 15;
constexpr int w_schedule_lag_16 = 16;

// Bit shifts for big-endian word assembly from bytes: FIPS 180-4, section 6.2.2.
constexpr int big_endian_shift_1 = 24;
constexpr int big_endian_shift_2 = 16;
constexpr int big_endian_shift_3 = 8;

// Byte positions in big-endian word: FIPS 180-4, section 6.2.2.
constexpr int word_byte_0 = 0;
constexpr int word_byte_1 = 1;
constexpr int word_byte_2 = 2;
constexpr int word_byte_3 = 3;

// Padding start byte: FIPS 180-4, section 5.1.1.
constexpr unsigned char padding_byte = 0x80;

// Length field size in bytes: FIPS 180-4, section 5.1.1.
constexpr int hash_length_bytes = 8;

// Bits per byte for length encoding: FIPS 180-4, section 5.1.1.
constexpr int bits_per_byte = 8;

// Buffer size for file streaming: power of 2 for efficiency.
constexpr size_t buffer_size = 1 << 16;

// Word size in bits for ROTR, FIPS 180-4 3.2.
constexpr int word_bits = 32;

// Hash state size in 32-bit words: FIPS 180-4, section 5.3.3.
constexpr int hash_state_words = 8;

// Bytes per word in message schedule: FIPS 180-4, section 6.2.2.
constexpr int bytes_per_word = 4;

// Hex string buffer size for one word: 8 hex digits + null terminator.
constexpr int hex_buffer_size = 9;

// Working variables a..h, FIPS 180-4, section 6.2.2.
constexpr size_t var_a = 0;
constexpr size_t var_b = 1;
constexpr size_t var_c = 2;
constexpr size_t var_d = 3;
constexpr size_t var_e = 4;
constexpr size_t var_f = 5;
constexpr size_t var_g = 6;
constexpr size_t var_h = 7;

uint32_t rv_editor_rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (word_bits - n));
}

void rv_editor_sha256_block(std::array<uint32_t, hash_state_words> &h, const unsigned char *block)
{
    std::array<uint32_t, message_schedule_full> w{};
    for (int i = 0; i < message_schedule_initial; ++i) {
        w[i] = static_cast<uint32_t>(block[i * bytes_per_word + word_byte_0]) << big_endian_shift_1 |
            static_cast<uint32_t>(block[i * bytes_per_word + word_byte_1]) << big_endian_shift_2 |
            static_cast<uint32_t>(block[i * bytes_per_word + word_byte_2]) << big_endian_shift_3 |
            static_cast<uint32_t>(block[i * bytes_per_word + word_byte_3]);
    }
    for (int i = message_schedule_initial; i < message_schedule_full; ++i) {
        const uint32_t s0 = rv_editor_rotr(w[i - w_schedule_lag_15], sigma0_rot_a) ^
            rv_editor_rotr(w[i - w_schedule_lag_15], sigma0_rot_b) ^ (w[i - w_schedule_lag_15] >> sigma0_rot_c);
        const uint32_t s1 = rv_editor_rotr(w[i - w_schedule_lag_2], sigma1_rot_a) ^
            rv_editor_rotr(w[i - w_schedule_lag_2], sigma1_rot_b) ^ (w[i - w_schedule_lag_2] >> sigma1_rot_c);
        w[i] = w[i - w_schedule_lag_16] + s0 + w[i - w_schedule_lag_7] + s1;
    }
    std::array<uint32_t, hash_state_words> v = h;
    for (int i = 0; i < message_schedule_full; ++i) {
        const uint32_t s1 = rv_editor_rotr(v[var_e], big_sigma1_rot_a) ^
            rv_editor_rotr(v[var_e], big_sigma1_rot_b) ^ rv_editor_rotr(v[var_e], big_sigma1_rot_c);
        const uint32_t ch = (v[var_e] & v[var_f]) ^ (~v[var_e] & v[var_g]);
        const uint32_t t1 = v[var_h] + s1 + ch + rv_editor_sha256_k[i] + w[i];
        const uint32_t s0 = rv_editor_rotr(v[var_a], big_sigma0_rot_a) ^
            rv_editor_rotr(v[var_a], big_sigma0_rot_b) ^ rv_editor_rotr(v[var_a], big_sigma0_rot_c);
        const uint32_t maj = (v[var_a] & v[var_b]) ^ (v[var_a] & v[var_c]) ^ (v[var_b] & v[var_c]);
        v = { t1 + s0 + maj, v[var_a], v[var_b], v[var_c], v[var_d] + t1, v[var_e], v[var_f], v[var_g] };
    }
    for (int i = 0; i < hash_state_words; ++i) {
        h[i] += v[i];
    }
}

} // namespace

std::string rv_editor_sha256_file(const std::filesystem::path &path, uint64_t &size, std::string &error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot read " + path.string();
        return {};
    }
    // Initial hash values: FIPS 180-4, section 5.3.3.
    std::array<uint32_t, hash_state_words> h = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f,
        0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    std::vector<unsigned char> buf(buffer_size);
    size = 0;
    size_t held = 0; // bytes of an unfinished block at the front of buf
    while (in) {
        in.read(reinterpret_cast<char *>(buf.data() + held), static_cast<std::streamsize>(buf.size() - held));
        const size_t got = static_cast<size_t>(in.gcount());
        size += got;
        held += got;
        size_t done = 0;
        for (; done + block_size <= held; done += block_size) {
            rv_editor_sha256_block(h, buf.data() + done);
        }
        std::copy(buf.begin() + static_cast<std::ptrdiff_t>(done), buf.begin() + static_cast<std::ptrdiff_t>(held),
            buf.begin());
        held -= done;
    }
    if (in.bad()) {
        error = "cannot read " + path.string();
        return {};
    }
    // The padding: 0x80, zeros, then the length in bits, big-endian, ending a block.
    constexpr size_t tail_size = block_size * 2;
    std::array<unsigned char, tail_size> tail{};
    std::copy(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(held), tail.begin());
    tail[held] = padding_byte;
    const size_t length_field_with_padding = hash_length_bytes + 1;
    const size_t blocks = held + length_field_with_padding > block_size ? 2 : 1;
    const uint64_t bits = size * bits_per_byte;
    for (int i = 0; i < hash_length_bytes; ++i) {
        tail[blocks * block_size - 1 - i] = static_cast<unsigned char>(bits >> (i * bits_per_byte));
    }
    for (size_t b = 0; b < blocks; ++b) {
        rv_editor_sha256_block(h, tail.data() + b * block_size);
    }
    std::string out;
    for (const uint32_t word : h) {
        char hex[hex_buffer_size];
        std::snprintf(hex, sizeof(hex), "%08x", word);
        out += hex;
    }
    return out;
}

} // namespace rv_editor
