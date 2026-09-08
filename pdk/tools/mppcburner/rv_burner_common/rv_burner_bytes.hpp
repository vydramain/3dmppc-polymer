#pragma once

#include <cstdint>
#include <vector>

namespace rv_pdktools
{

// --- little-endian scalars ---
//
// Both file formats this tool touches — the .mppcdisc container and the
// .mppctex the baker writes — store their integers little-endian, because that
// is what zip specifies and what the console reads. Assembling and taking apart
// those integers BYTE BY BYTE, rather than memcpy'ing a native integer, is what
// makes that true independently of the machine the burner runs on: the same code
// produces the same file on a big-endian host.
//
// Every function here is inline and takes no ownership; a caller that reads must
// have checked the bounds first, because a pointer cannot.

/// Read a little-endian uint16 from two bytes at @p p.
inline uint16_t read_le_u16(const unsigned char *p)
{
    return static_cast<uint16_t>(
        static_cast<uint16_t>(p[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8));
}

/// Read a little-endian uint32 from four bytes at @p p.
inline uint32_t read_le_u32(const unsigned char *p)
{
    return static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}

/// Append @p value to @p out as two little-endian bytes.
inline void put_le_u16(std::vector<uint8_t> &out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

/// Append @p value to @p out as four little-endian bytes.
inline void put_le_u32(std::vector<uint8_t> &out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

} // namespace rv_pdktools
