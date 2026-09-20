#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rv_pdklib
{

// --- the .mppcdisc container: the zip layout knowledge two readers share ---
//
// A .mppcdisc is a plain zip in which every entry is STORED (method 0, no
// compression) — see rv_zip_method_store below for why. Two programs parse
// that layout: the console's reader (src/rv_pconsole/cd/rv_zipreader.*), which
// treats every byte as attacker-controlled and refuses anything it cannot
// prove safe, and the authoring tool's reader/writer
// (pdk/tools/mppcburner/rv_burner_zip/*), which trusts a file the developer
// just produced on their own machine. Both used to restate the signatures, the
// fixed record sizes and the field offsets separately, and the two copies could
// drift — a changed offset right on one side and stale on the other. This
// header is the one place those numbers live; each side still does its own
// validation and its own I/O, it only stops re-deriving what a byte at a given
// offset means.
//
// Nothing here reads a file or a socket: every function takes an in-memory
// byte span the caller has already carved out of a buffer it owns.

// --- record signatures, as the 32-bit little-endian values they decode to ---

inline constexpr uint32_t rv_zip_sig_local = 0x04034b50u;    // PK\x03\x04
inline constexpr uint32_t rv_zip_sig_central = 0x02014b50u;  // PK\x01\x02
inline constexpr uint32_t rv_zip_sig_eocd = 0x06054b50u;     // PK\x05\x06

// --- fixed parts of the three records, signature included, any
// variable-length tail (name, extra field, comment) excluded ---

inline constexpr std::size_t rv_zip_local_header_size = 30;
inline constexpr std::size_t rv_zip_central_header_size = 46;
inline constexpr std::size_t rv_zip_eocd_size = 22;

// The archive comment length is a uint16 field, so the EOCD record can sit at
// most this many bytes away from the end of the file.
inline constexpr std::size_t rv_zip_max_comment_size = 0xFFFFu;

// The only compression method a .mppcdisc entry may use. A stored entry is
// served with one seek and one read at an offset the archive itself hands
// out, which is what lets the console skip linking a decompressor.
inline constexpr uint16_t rv_zip_method_store = 0;

// An all-ones field is zip's "the real value is in a zip64 extra record"
// sentinel. Neither side here supports zip64; a reader that finds one of these
// is expected to refuse the entry or archive rather than treat the sentinel as
// a real 32-bit (or 16-bit) number.
inline constexpr uint32_t rv_zip_zip64_sentinel32 = 0xFFFFFFFFu;
inline constexpr uint16_t rv_zip_zip64_sentinel16 = 0xFFFFu;

// --- field offsets inside a local file header ---

enum rv_zip_local_offset : std::size_t {
    RV_ZIP_LOCAL_OFF_SIG = 0,
    RV_ZIP_LOCAL_OFF_METHOD = 8,
    RV_ZIP_LOCAL_OFF_NAME_LENGTH = 26,
    RV_ZIP_LOCAL_OFF_EXTRA_LENGTH = 28,
};

// --- field offsets inside a central directory header ---

enum rv_zip_central_offset : std::size_t {
    RV_ZIP_CENTRAL_OFF_SIG = 0,
    RV_ZIP_CENTRAL_OFF_METHOD = 10,
    RV_ZIP_CENTRAL_OFF_CRC = 16,
    RV_ZIP_CENTRAL_OFF_COMPRESSED_SIZE = 20,
    RV_ZIP_CENTRAL_OFF_UNCOMPRESSED_SIZE = 24,
    RV_ZIP_CENTRAL_OFF_NAME_LENGTH = 28,
    RV_ZIP_CENTRAL_OFF_EXTRA_LENGTH = 30,
    RV_ZIP_CENTRAL_OFF_COMMENT_LENGTH = 32,
    RV_ZIP_CENTRAL_OFF_LOCAL_OFFSET = 42,
};

// --- field offsets inside the end-of-central-directory record ---

enum rv_zip_eocd_offset : std::size_t {
    RV_ZIP_EOCD_OFF_SIG = 0,
    RV_ZIP_EOCD_OFF_DISK_NUMBER = 4,
    RV_ZIP_EOCD_OFF_CD_DISK = 6,
    RV_ZIP_EOCD_OFF_ENTRIES_ON_DISK = 8,
    RV_ZIP_EOCD_OFF_ENTRY_COUNT = 10,
    RV_ZIP_EOCD_OFF_DIRECTORY_SIZE = 12,
    RV_ZIP_EOCD_OFF_DIRECTORY_OFFSET = 16,
    RV_ZIP_EOCD_OFF_COMMENT_LENGTH = 20,
};

// --- decoded records ---
//
// One struct per fixed record, holding only the fields either reader actually
// consults. Decoding is a separate step from validating: these structs carry
// whatever bytes were at the given offsets, unchecked, exactly the way each
// side's inline reads always worked before they moved here.

/// A local file header (30 fixed bytes, PK\x03\x04), decoded but not validated.
struct rv_zip_local_header {
    uint32_t signature = 0;
    uint16_t method = 0;
    uint16_t name_length = 0;
    uint16_t extra_length = 0;
};

/// A central directory file header (46 fixed bytes, PK\x01\x02), decoded but
/// not validated.
struct rv_zip_central_header {
    uint32_t signature = 0;
    uint16_t method = 0;
    uint32_t crc32 = 0;
    uint32_t compressed_size = 0;
    uint32_t uncompressed_size = 0;
    uint16_t name_length = 0;
    uint16_t extra_length = 0;
    uint16_t comment_length = 0;
    uint32_t local_header_offset = 0;
};

/// An end-of-central-directory record (22 fixed bytes, PK\x05\x06), decoded
/// but not validated.
struct rv_zip_eocd {
    uint32_t signature = 0;
    uint16_t disk_number = 0;
    uint16_t cd_disk = 0;
    uint16_t entries_on_disk = 0;
    uint16_t entries_total = 0;
    uint32_t cd_size = 0;
    uint32_t cd_offset = 0;
    uint16_t comment_length = 0;
};

// --- little-endian field readers ---
//
// Not zip-specific and not exported as part of the format: just what the
// decode functions below need to turn two or four bytes at a known offset
// into a scalar, spelled out byte by byte so the result does not depend on
// the host's endianness or on a struct's padding.

inline uint16_t rv_zip_read_le16(const unsigned char *p)
{
    return static_cast<uint16_t>(static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8));
}

inline uint32_t rv_zip_read_le32(const unsigned char *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// --- decoding ---
//
// Each function reads exactly the fixed record out of `bytes`, which the
// caller must have already sized to at least the matching *_header_size (the
// way it already had to bounds-check before its own inline rd16/rd32 calls) —
// this function trusts that and nothing else; a zip's own fields cannot be
// trusted to describe themselves, so every check beyond "these are the bytes
// at these offsets" stays with the caller, and the two callers do not agree
// on all of those checks.

/// Decode a local file header. @p bytes must be at least rv_zip_local_header_size long.
inline rv_zip_local_header rv_zip_decode_local_header(std::span<const unsigned char> bytes)
{
    rv_zip_local_header header;
    header.signature = rv_zip_read_le32(bytes.data() + RV_ZIP_LOCAL_OFF_SIG);
    header.method = rv_zip_read_le16(bytes.data() + RV_ZIP_LOCAL_OFF_METHOD);
    header.name_length = rv_zip_read_le16(bytes.data() + RV_ZIP_LOCAL_OFF_NAME_LENGTH);
    header.extra_length = rv_zip_read_le16(bytes.data() + RV_ZIP_LOCAL_OFF_EXTRA_LENGTH);
    return header;
}

/// Decode a central directory file header. @p bytes must be at least
/// rv_zip_central_header_size long.
inline rv_zip_central_header rv_zip_decode_central_header(std::span<const unsigned char> bytes)
{
    rv_zip_central_header header;
    header.signature = rv_zip_read_le32(bytes.data() + RV_ZIP_CENTRAL_OFF_SIG);
    header.method = rv_zip_read_le16(bytes.data() + RV_ZIP_CENTRAL_OFF_METHOD);
    header.crc32 = rv_zip_read_le32(bytes.data() + RV_ZIP_CENTRAL_OFF_CRC);
    header.compressed_size = rv_zip_read_le32(bytes.data() + RV_ZIP_CENTRAL_OFF_COMPRESSED_SIZE);
    header.uncompressed_size = rv_zip_read_le32(bytes.data() + RV_ZIP_CENTRAL_OFF_UNCOMPRESSED_SIZE);
    header.name_length = rv_zip_read_le16(bytes.data() + RV_ZIP_CENTRAL_OFF_NAME_LENGTH);
    header.extra_length = rv_zip_read_le16(bytes.data() + RV_ZIP_CENTRAL_OFF_EXTRA_LENGTH);
    header.comment_length = rv_zip_read_le16(bytes.data() + RV_ZIP_CENTRAL_OFF_COMMENT_LENGTH);
    header.local_header_offset = rv_zip_read_le32(bytes.data() + RV_ZIP_CENTRAL_OFF_LOCAL_OFFSET);
    return header;
}

/// Decode an end-of-central-directory record. @p bytes must be at least
/// rv_zip_eocd_size long.
inline rv_zip_eocd rv_zip_decode_eocd(std::span<const unsigned char> bytes)
{
    rv_zip_eocd record;
    record.signature = rv_zip_read_le32(bytes.data() + RV_ZIP_EOCD_OFF_SIG);
    record.disk_number = rv_zip_read_le16(bytes.data() + RV_ZIP_EOCD_OFF_DISK_NUMBER);
    record.cd_disk = rv_zip_read_le16(bytes.data() + RV_ZIP_EOCD_OFF_CD_DISK);
    record.entries_on_disk = rv_zip_read_le16(bytes.data() + RV_ZIP_EOCD_OFF_ENTRIES_ON_DISK);
    record.entries_total = rv_zip_read_le16(bytes.data() + RV_ZIP_EOCD_OFF_ENTRY_COUNT);
    record.cd_size = rv_zip_read_le32(bytes.data() + RV_ZIP_EOCD_OFF_DIRECTORY_SIZE);
    record.cd_offset = rv_zip_read_le32(bytes.data() + RV_ZIP_EOCD_OFF_DIRECTORY_OFFSET);
    record.comment_length = rv_zip_read_le16(bytes.data() + RV_ZIP_EOCD_OFF_COMMENT_LENGTH);
    return record;
}

}  // namespace rv_pdklib
