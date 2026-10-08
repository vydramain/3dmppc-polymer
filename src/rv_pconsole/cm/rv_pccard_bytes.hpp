#pragma once

// Layout and little-endian field codec of the card image.

#include <cstdint>

namespace rv_3dmppc::rv_pccard_detail
{

// Bit and byte operations
constexpr int BITS_PER_BYTE = 8;
constexpr uint8_t BYTE_MASK = 0xFFu;

// Integer type sizes for put/get operations
constexpr int BYTES_PER_U32 = sizeof(uint32_t);
constexpr int BYTES_PER_I64 = sizeof(int64_t);

// Card image layout: magic[8] version[4] reserved[4] slot_count[8] slot_size[8] then slot lengths[8..].
constexpr int64_t HEADER_MAGIC_SIZE = 8;
constexpr int64_t HEADER_OFFSET_VERSION = HEADER_MAGIC_SIZE;
constexpr int64_t HEADER_OFFSET_RESERVED = HEADER_OFFSET_VERSION + BYTES_PER_U32;
constexpr int64_t HEADER_OFFSET_SLOT_COUNT = HEADER_OFFSET_RESERVED + BYTES_PER_U32;
constexpr int64_t HEADER_OFFSET_SLOT_SIZE = HEADER_OFFSET_SLOT_COUNT + BYTES_PER_I64;
constexpr int64_t RV_PCCARD_HEADER_SIZE = HEADER_OFFSET_SLOT_SIZE + BYTES_PER_I64;
constexpr int64_t RV_PCCARD_LENGTH_ENTRY = BYTES_PER_I64;

// Explicit byte order - the image is written and read one byte at a
// time so a card written on one machine stays readable on another, instead of
// silently inheriting whatever layout the compiler gave an int64_t.
inline void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < BYTES_PER_U32; ++i) {
        p[i] = static_cast<uint8_t>((v >> (BITS_PER_BYTE * i)) & BYTE_MASK);
    }
}

inline uint32_t get_u32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 0; i < BYTES_PER_U32; ++i) {
        v |= static_cast<uint32_t>(p[i]) << (BITS_PER_BYTE * i);
    }
    return v;
}

inline void put_i64(uint8_t *p, int64_t v)
{
    const uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < BYTES_PER_I64; ++i) {
        p[i] = static_cast<uint8_t>((u >> (BITS_PER_BYTE * i)) & BYTE_MASK);
    }
}

inline int64_t get_i64(const uint8_t *p)
{
    uint64_t u = 0;
    for (int i = 0; i < BYTES_PER_I64; ++i) {
        u |= static_cast<uint64_t>(p[i]) << (BITS_PER_BYTE * i);
    }
    return static_cast<int64_t>(u);
}

} // namespace rv_3dmppc::rv_pccard_detail
