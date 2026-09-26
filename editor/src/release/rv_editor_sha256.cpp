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

constexpr std::array<uint32_t, 64> rv_editor_sha256_k = { 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc,
    0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1,
    0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814,
    0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

uint32_t rv_editor_rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

void rv_editor_sha256_block(std::array<uint32_t, 8> &h, const unsigned char *block)
{
    std::array<uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[i] = static_cast<uint32_t>(block[i * 4]) << 24 | static_cast<uint32_t>(block[i * 4 + 1]) << 16 |
            static_cast<uint32_t>(block[i * 4 + 2]) << 8 | static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rv_editor_rotr(w[i - 15], 7) ^ rv_editor_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rv_editor_rotr(w[i - 2], 17) ^ rv_editor_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::array<uint32_t, 8> v = h;
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = rv_editor_rotr(v[4], 6) ^ rv_editor_rotr(v[4], 11) ^ rv_editor_rotr(v[4], 25);
        const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const uint32_t t1 = v[7] + s1 + ch + rv_editor_sha256_k[i] + w[i];
        const uint32_t s0 = rv_editor_rotr(v[0], 2) ^ rv_editor_rotr(v[0], 13) ^ rv_editor_rotr(v[0], 22);
        const uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        v = { t1 + s0 + maj, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6] };
    }
    for (int i = 0; i < 8; ++i) {
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
    std::array<uint32_t, 8> h = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19 };
    std::vector<unsigned char> buf(1 << 16);
    size = 0;
    size_t held = 0; // bytes of an unfinished block at the front of buf
    while (in) {
        in.read(reinterpret_cast<char *>(buf.data() + held), static_cast<std::streamsize>(buf.size() - held));
        const size_t got = static_cast<size_t>(in.gcount());
        size += got;
        held += got;
        size_t done = 0;
        for (; done + 64 <= held; done += 64) {
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
    std::array<unsigned char, 128> tail{};
    std::copy(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(held), tail.begin());
    tail[held] = 0x80;
    const size_t blocks = held + 9 > 64 ? 2 : 1;
    const uint64_t bits = size * 8;
    for (int i = 0; i < 8; ++i) {
        tail[blocks * 64 - 1 - i] = static_cast<unsigned char>(bits >> (i * 8));
    }
    for (size_t b = 0; b < blocks; ++b) {
        rv_editor_sha256_block(h, tail.data() + b * 64);
    }
    std::string out;
    for (const uint32_t word : h) {
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%08x", word);
        out += hex;
    }
    return out;
}

} // namespace rv_editor
