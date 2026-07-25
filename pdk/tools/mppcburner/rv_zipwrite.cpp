// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 10 — запись zip-контейнера .mppcdisc методом store.
// ──────────────────────────────────────────────────────────────────────────────
//
// The writer half of the container described in rv_zipwrite.hpp. Three record
// kinds, in this order:
//
//   [local header + data] * n   local file headers, PK\x03\x04
//   [central header] * n        the directory, PK\x01\x02
//   [end of central directory]  PK\x05\x06
//
// The reader on the console (src/rv_pconsole/cd/rv_pczip.*) walks the central
// directory, so a well-formed one is what actually makes the archive readable;
// the local headers are there because every other zip tool in the world expects
// them, and being openable by `unzip` is how a developer checks a burn without
// trusting our own code.

#include "rv_zipwrite.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

namespace rv_pdktools {
namespace {

// --- constants ----------------------------------------------------------------

constexpr uint32_t kSigLocal = 0x04034b50u;    // "PK\x03\x04"
constexpr uint32_t kSigCentral = 0x02014b50u;  // "PK\x01\x02"
constexpr uint32_t kSigEocd = 0x06054b50u;     // "PK\x05\x06"

// 2.0 — the version that introduced the deflate method and the folder entry.
// Nothing here needs more, and claiming more would refuse readers that could
// have coped perfectly well.
constexpr uint16_t kVersion = 20;

// Bit 11 of the general-purpose flags: the entry name (and comment) are UTF-8.
// Without it a name is officially CP437 and any reader is entitled to mangle
// every byte above 0x7F — including the ones in a Cyrillic asset name.
constexpr uint16_t kFlagUtf8 = 0x0800;

// "Version made by": the low byte is the zip version (2.0), the HIGH byte is the
// host filesystem the archive was made on — 3 is Unix, 0 would be MS-DOS/FAT.
// Declaring Unix is not cosmetic: Info-ZIP's unzip pushes the name of any
// FAT-hosted entry through a CP437 -> local charset translation *before* it
// consults the UTF-8 flag above, so a host byte of 0 turns "привет.txt" into
// mojibake on extraction even though bit 11 is set. Verified against unzip 6.0.
constexpr uint16_t kVersionMadeBy = (3 << 8) | kVersion;

// External file attributes, in the layout the Unix host implies: the high 16
// bits are the st_mode of the entry. 0100644 — a regular file, rw-r--r--.
// A FIXED mode, not the mode of whatever file the bytes were read from: the
// same reason the timestamp below is fixed. A disc is a function of its content,
// and a developer's umask is not part of its content. (A host byte of 3 with
// zero attributes would extract as a file with no permission bits at all, which
// is the trap this constant exists to avoid.)
constexpr uint32_t kExternalAttrs = 0100644u << 16;

constexpr uint16_t kMethodStore = 0;  // no compression, ever — see rv_zipwrite.hpp

// PATTERN: deterministic timestamp. The MS-DOS date/time in every header is a
// fixed constant instead of the current clock, so burning the same directory
// twice produces a BYTE-IDENTICAL .mppcdisc. That is what makes an artefact
// comparison meaningful ("did this change?" is answered by cmp, not by reading
// a diff of noise), what lets a build cache key an archive by its hash, and what
// keeps a reproducible-build check from failing for no reason. A file's real
// mtime is a property of the filesystem it came from, not of the disc; the disc
// is a function of its inputs and nothing else. The value is 1980-01-01 00:00:00
// — the earliest instant the DOS format can express, chosen precisely because it
// is unmistakably a sentinel rather than a plausible wall-clock time.
//   time: hh<<11 | mm<<5 | (ss/2)     -> 00:00:00
//   date: (yyyy-1980)<<9 | mm<<5 | dd -> 1980-01-01
constexpr uint16_t kDosTime = 0;
constexpr uint16_t kDosDate = (0 << 9) | (1 << 5) | 1;  // 0x0021

// Neither the sizes nor the entry count may leave the 32/16-bit fields of the
// classic format: zip64 is a second format, and half-writing it would produce
// an archive that some readers accept and others silently truncate.
constexpr uint64_t kMaxSize = 0xFFFFFFFFull;
constexpr std::size_t kMaxEntries = 0xFFFFu;
constexpr std::size_t kMaxNameLength = 0xFFFFu;

// --- THEOREM: CRC32 -----------------------------------------------------------
//
// Every entry carries a CRC-32 of its uncompressed bytes, and the console-side
// reader recomputes it and refuses the entry when it disagrees. This is not
// ceremony. The failure mode a disc actually has is SILENT corruption: a burn
// interrupted midway, a truncated copy over a flaky transfer, a texture blob
// half-overwritten by a tool that crashed. None of those break the zip
// structure — the offsets still point somewhere, the sizes still add up — so
// without a checksum the reader hands the game plausible garbage and the bug
// surfaces as a texture full of noise or a mesh with a spike through it,
// arbitrarily far from the actual damage. A 32-bit CRC turns "wrong pixels
// somewhere" into "entry 'hero.mppctex' is corrupt", which is a diagnosis.
//
// The polynomial is 0xEDB88320: the standard CRC-32 (IEEE 802.3) polynomial
// 0x04C11DB7 written REFLECTED — its bits reversed. That is not a different
// algorithm, it is the same one arranged for the direction the bytes arrive in.
// The mathematical definition shifts the register towards the most significant
// bit and feeds each byte MSB-first; reflecting the polynomial lets the register
// shift RIGHT instead and consume each byte LSB-first, so a whole byte can be
// folded in with one table lookup and one shift, with no bit-reversal at either
// end. Both forms compute the same remainder over GF(2), and only the reflected
// form matches what the zip specification's test vectors and every other zip
// implementation produce — a "CRC32" built from the unreflected polynomial
// would be self-consistent and rejected by everyone.
//
// The pre-inversion (~0) and post-inversion of the register are also part of the
// standard: they are what makes leading zero bytes and trailing zero bytes
// change the result, so a truncation to zeros — the exact shape of an
// interrupted burn — cannot pass unnoticed.
constexpr uint32_t kCrcPolynomial = 0xEDB88320u;

struct crc32_table {
    uint32_t entry[256] = {};

    constexpr crc32_table() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1u) ? (kCrcPolynomial ^ (c >> 1)) : (c >> 1);
            }
            entry[i] = c;
        }
    }
};

constexpr crc32_table kCrcTable{};

uint32_t crc32_of(const void* data, std::size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = kCrcTable.entry[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// --- byte-exact serialisation -------------------------------------------------
//
// Every multi-byte field is emitted one byte at a time, little-endian, rather
// than by memcpy-ing a packed struct over it. The file format fixes its layout
// forever; a C++ struct does not. The compiler may insert padding between
// members and may align the whole thing, `#pragma pack` is not portable, and on
// a big-endian host a memcpy would write the bytes in the wrong order and
// produce an archive that is corrupt in a way nothing on this machine can see.
// Writing the bytes by hand costs nothing measurable — the payload dominates —
// and makes the code read exactly like the format table it implements.

void put_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void put_bytes(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
}

// What the central directory has to remember about an entry once its data is
// already on disk.
struct zip_entry {
    std::string name;
    uint32_t crc = 0;
    uint32_t size = 0;
    uint32_t local_offset = 0;
};

}  // namespace

struct rv_zipwriter::rv_zipwriter_impl {
    std::string path;
    std::ofstream out;
    bool opened = false;
    bool finished = false;
    bool broken = false;  // an I/O error already happened; the file is worthless
    uint64_t offset = 0;  // bytes written so far == offset of the next record
    std::vector<zip_entry> entries;
    std::vector<std::string> names;

    bool write(const void* data, std::size_t size) {
        if (size == 0) return true;
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!out) {
            broken = true;
            return false;
        }
        offset += size;
        return true;
    }

    bool write(const std::vector<uint8_t>& bytes) { return write(bytes.data(), bytes.size()); }

    // Deletes the half-written file. A truncated archive that still has a
    // plausible name is worse than no file at all: the next step of the build,
    // or the player, will pick it up and fail somewhere far away from here.
    void discard() {
        if (out.is_open()) out.close();
        if (!path.empty()) std::remove(path.c_str());
    }
};

rv_zipwriter::rv_zipwriter(const std::string& path) : impl_(new rv_zipwriter_impl()) {
    impl_->path = path;
    impl_->out.open(path, std::ios::binary | std::ios::trunc);
    impl_->opened = impl_->out.is_open();
}

rv_zipwriter::~rv_zipwriter() {
    // finish() was never called, or it failed: whatever is on disk is not an
    // archive, so it does not get to keep the name of one.
    if (impl_->opened && !impl_->finished) impl_->discard();
    delete impl_;
}

bool rv_zipwriter::ok() const { return impl_->opened && !impl_->broken && !impl_->finished; }

bool rv_zipwriter::add(const std::string& name, const void* data, std::size_t size,
                       std::string& error) {
    error.clear();
    if (!impl_->opened) {
        error = "archive '" + impl_->path + "' is not open";
        return false;
    }
    if (impl_->finished) {
        error = "archive '" + impl_->path + "' is already finished";
        return false;
    }
    if (impl_->broken) {
        error = "archive '" + impl_->path + "' already failed to write";
        return false;
    }
    if (name.empty()) {
        error = "entry name is empty";
        return false;
    }
    if (name.size() > kMaxNameLength) {
        error = "entry name is longer than 65535 bytes";
        return false;
    }

    // A duplicate is refused rather than appended. Every reader resolves a name
    // by taking the FIRST match in the directory, so a silent second copy means
    // the author is sure they shipped the new file while the console keeps
    // handing out the old one — a disagreement that survives every reburn and
    // explains nothing when it finally shows up.
    if (std::find(impl_->names.begin(), impl_->names.end(), name) != impl_->names.end()) {
        error = "duplicate entry name '" + name + "'";
        return false;
    }

    if (impl_->entries.size() >= kMaxEntries) {
        error = "too many entries for a classic zip (limit 65535)";
        return false;
    }
    if (static_cast<uint64_t>(size) > kMaxSize) {
        error = "entry '" + name + "' is larger than 4 GiB, which needs zip64";
        return false;
    }
    // Local header + name + data must also stay inside a 32-bit offset, because
    // that is the width of the field the central directory points back with.
    if (impl_->offset + 30 + name.size() + size > kMaxSize) {
        error = "archive would exceed 4 GiB, which needs zip64";
        return false;
    }

    zip_entry entry;
    entry.name = name;
    entry.crc = crc32_of(data, size);
    entry.size = static_cast<uint32_t>(size);
    entry.local_offset = static_cast<uint32_t>(impl_->offset);

    // Local file header, 30 bytes plus the name. Stored, so the compressed and
    // uncompressed sizes are the same number written twice.
    std::vector<uint8_t> header;
    header.reserve(30 + name.size());
    put_u32(header, kSigLocal);
    put_u16(header, kVersion);      // version needed to extract
    put_u16(header, kFlagUtf8);     // general purpose flags
    put_u16(header, kMethodStore);  // compression method
    put_u16(header, kDosTime);
    put_u16(header, kDosDate);
    put_u32(header, entry.crc);
    put_u32(header, entry.size);  // compressed size
    put_u32(header, entry.size);  // uncompressed size
    put_u16(header, static_cast<uint16_t>(name.size()));
    put_u16(header, 0);  // extra field length
    put_bytes(header, name);

    if (!impl_->write(header) || !impl_->write(data, size)) {
        error = "cannot write entry '" + name + "' to '" + impl_->path + "'";
        return false;
    }

    impl_->entries.push_back(entry);
    impl_->names.push_back(name);
    return true;
}

bool rv_zipwriter::add_file(const std::string& name, const std::string& source_path,
                            std::string& error) {
    error.clear();
    std::ifstream in(source_path, std::ios::binary);
    if (!in) {
        error = "cannot open '" + source_path + "'";
        return false;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        error = "cannot read '" + source_path + "'";
        return false;
    }
    return add(name, data.data(), data.size(), error);
}

bool rv_zipwriter::finish(std::string& error) {
    error.clear();
    if (!impl_->opened) {
        error = "archive '" + impl_->path + "' is not open";
        return false;
    }
    if (impl_->finished) {
        error = "archive '" + impl_->path + "' is already finished";
        return false;
    }
    if (impl_->broken) {
        error = "archive '" + impl_->path + "' failed to write earlier";
        impl_->discard();
        impl_->finished = true;
        return false;
    }

    const uint64_t directory_offset = impl_->offset;

    std::vector<uint8_t> directory;
    for (const zip_entry& e : impl_->entries) {
        // Central directory file header, 46 bytes plus the name.
        put_u32(directory, kSigCentral);
        put_u16(directory, kVersionMadeBy);
        put_u16(directory, kVersion);  // version needed to extract
        put_u16(directory, kFlagUtf8);
        put_u16(directory, kMethodStore);
        put_u16(directory, kDosTime);
        put_u16(directory, kDosDate);
        put_u32(directory, e.crc);
        put_u32(directory, e.size);  // compressed size
        put_u32(directory, e.size);  // uncompressed size
        put_u16(directory, static_cast<uint16_t>(e.name.size()));
        put_u16(directory, 0);               // extra field length
        put_u16(directory, 0);               // file comment length
        put_u16(directory, 0);               // disk number start
        put_u16(directory, 0);               // internal file attributes
        put_u32(directory, kExternalAttrs);  // external file attributes
        put_u32(directory, e.local_offset);
        put_bytes(directory, e.name);
    }

    if (directory_offset + directory.size() > kMaxSize) {
        error = "archive would exceed 4 GiB, which needs zip64";
        impl_->discard();
        impl_->finished = true;
        return false;
    }

    std::vector<uint8_t> eocd;
    const uint16_t count = static_cast<uint16_t>(impl_->entries.size());
    put_u32(eocd, kSigEocd);
    put_u16(eocd, 0);      // this disk number
    put_u16(eocd, 0);      // disk with the start of the central directory
    put_u16(eocd, count);  // entries on this disk
    put_u16(eocd, count);  // entries in total
    put_u32(eocd, static_cast<uint32_t>(directory.size()));
    put_u32(eocd, static_cast<uint32_t>(directory_offset));
    put_u16(eocd, 0);  // archive comment length

    if (!impl_->write(directory) || !impl_->write(eocd)) {
        error = "cannot write central directory of '" + impl_->path + "'";
        impl_->discard();
        impl_->finished = true;
        return false;
    }

    impl_->out.flush();
    const bool good = static_cast<bool>(impl_->out);
    impl_->out.close();
    // close() is the last chance for a buffered write to fail — a full disk
    // usually shows up here and nowhere earlier.
    if (!good || impl_->out.fail()) {
        error = "cannot close '" + impl_->path + "'";
        impl_->discard();
        impl_->finished = true;
        return false;
    }

    impl_->finished = true;
    return true;
}

const std::vector<std::string>& rv_zipwriter::entries() const { return impl_->names; }

}  // namespace rv_pdktools
